#pragma once
#include "mono_field.h"

#include "mono_object.h"
#include "mono_type_conversion.h"
namespace mono
{

template <typename T>
class mono_field_invoker : public mono_field
{
public:
	using value_type = T;

	void set_value(const T& val) const;

	void set_value(const mono_object& obj, const T& val) const;

	auto get_value() const -> T;

	auto get_value(const mono_object& obj) const -> T;

private:
	template <typename signature_t>
	friend auto make_field_invoker(const mono_field&) -> mono_field_invoker<signature_t>;

	explicit mono_field_invoker(const mono_field& field)
		: mono_field(field)
	{
	}

	void set_value_impl(const mono_object* obj, const T& val) const;

	auto get_value_impl(const mono_object* obj) const -> T;
};

template <typename T>
void mono_field_invoker<T>::set_value(const T& val) const
{
	set_value_impl(nullptr, val);
}

template <typename T>
void mono_field_invoker<T>::set_value(const mono_object& object, const T& val) const
{
	set_value_impl(&object, val);
}

template <typename T>
void mono_field_invoker<T>::set_value_impl(const mono_object* object, const T& val) const
{
	assert(field_);

	auto mono_val = mono_converter<T>::to_mono(val);
	auto arg = to_mono_arg(mono_val, type_);

	if(object)
	{
		auto obj = object->get_internal_ptr();
		assert(obj);
		mono_field_set_value(obj, field_, arg);
	}
	else
	{
		mono_field_static_set_value(owning_type_vtable_, field_, arg);
	}
}

template <>
inline void mono_field_invoker<mono_object>::set_value_impl(const mono_object* object,
                                                             const mono_object& val) const
{
    assert(field_);

    MonoType* ftype = mono_field_get_type(field_);
    MonoClass* fklass = mono_class_from_mono_type(ftype);
    const bool field_is_valuetype = mono_class_is_valuetype(fklass) != 0;

    MonoObject* value_obj = val.get_internal_ptr(); // may be null

    if (!field_is_valuetype)
    {
        // Reference-type field (class/interface/array/string)
        if (value_obj)
        {
            MonoClass* vklass = mono_object_get_class(value_obj);
            if (!mono_class_is_assignable_from(fklass, vklass))
                throw std::runtime_error("set_value(mono_object): value not assignable to reference field");
        }

        if (object)
        {
            MonoObject* inst = object->get_internal_ptr();
            assert(inst);
            // Pass address of MonoObject* for reference fields
            mono_field_set_value(inst, field_, value_obj);
        }
        else
        {
            mono_field_static_set_value(owning_type_vtable_, field_, value_obj);
        }
    }
    else
    {
        // Valuetype field (struct)
        if (!value_obj)
            return;

        MonoClass* vklass = mono_object_get_class(value_obj);
        if (!mono_class_is_valuetype(vklass))
            return;

        if (vklass != fklass)
            return;

        // For valuetypes you pass a pointer to the raw data (unboxed)
        void* unboxed = mono_object_unbox(value_obj);

        if (object)
        {
            MonoObject* inst = object->get_internal_ptr();
            assert(inst);
            mono_field_set_value(inst, field_, unboxed);            // copies bytes
        }
        else
        {
            mono_field_static_set_value(owning_type_vtable_, field_, unboxed);
        }
    }
}

template <typename T>
auto mono_field_invoker<T>::get_value() const -> T
{
	return get_value_impl(nullptr);
}

template <typename T>
auto mono_field_invoker<T>::get_value(const mono_object& object) const -> T
{
	return get_value_impl(&object);
}

template <typename T>
auto mono_field_invoker<T>::get_value_impl(const mono_object* object) const -> T
{
	T val{};
	assert(field_);
	MonoObject* refvalue = nullptr;
	void* arg = &val;
	if(!is_valuetype())
	{
		arg = &refvalue;
	}
	if(object)
	{
		auto obj = object->get_internal_ptr();
		assert(obj);
		mono_field_get_value(obj, field_, arg);
	}
	else
	{
		mono_field_static_get_value(owning_type_vtable_, field_, arg);
	}

	if(!is_valuetype())
	{
		val = mono_converter<T>::from_mono(refvalue);
	}
	return val;
}

template <>
inline auto mono_field_invoker<mono_object>::get_value_impl(const mono_object* object) const -> mono_object
{
    assert(field_);
    MonoDomain* domain = mono_domain_get();
    MonoObject* result = nullptr;

	MonoType* ftype = mono_field_get_type(field_);
	MonoClass* fklass = mono_class_from_mono_type(ftype);

	// Safer check than mono_type_is_reference for value types
	const bool is_value_type = mono_class_is_valuetype(fklass) != 0;
    if (object)
    {

		MonoObject* inst = object->get_internal_ptr();
		assert(inst);
		
		if (!is_value_type)
        {
			// Instance field: let Mono do the right thing (auto-box value types)
			result = mono_field_get_value_object(domain, field_, inst);
		}
		else
		{
			// Value-type static field: read raw bytes then box
			uint32_t align = 0;
			size_t size = mono_class_value_size(fklass, &align);
			std::vector<std::uint8_t> buffer(size);
			mono_field_get_value(inst, field_, buffer.data());
			result = mono_value_box(domain, fklass, buffer.data());
		}
    }
    else
    {

        if (!is_value_type)
        {
            // Reference-type static field: fetch MonoObject* directly
            MonoObject* tmp = nullptr;
            mono_field_static_get_value(owning_type_vtable_, field_, &tmp);
            result = tmp;
        }
        else
        {
            // Value-type static field: read raw bytes then box
            uint32_t align = 0;
            size_t size = mono_class_value_size(fklass, &align);
            std::vector<std::uint8_t> buffer(size);
            mono_field_static_get_value(owning_type_vtable_, field_, buffer.data());
            result = mono_value_box(domain, fklass, buffer.data());
        }
    }

    return mono_object(result);
}

template <typename T>
auto make_field_invoker(const mono_field& field) -> mono_field_invoker<T>
{
	return mono_field_invoker<T>(field);
}

template <typename T>
auto make_field_invoker(const mono_type& type, const std::string& name) -> mono_field_invoker<T>
{
	auto field = type.get_field(name);
	return make_field_invoker<T>(field);
}

template <typename T>
auto make_invoker(const mono_field& field) -> mono_field_invoker<T>
{
	return make_field_invoker<T>(field);
}

template <typename T>
auto set_field_value(const mono_object& obj, const std::string& name, const T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(obj.get_type(), name);
		invoker.set_value(obj, val);
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}


template <typename T>
auto set_field_value(const mono_type& type, const std::string& name, const T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(type, name);
		invoker.set_value(val);
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

template <typename T>
auto get_field_value(const mono_object& obj, const std::string& name, T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(obj.get_type(), name);
		val = invoker.get_value(obj);
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}

template <typename T>
auto get_field_value(const mono_type& type, const std::string& name, T& val) -> bool
{
	try
	{
		auto invoker = make_field_invoker<T>(type, name);
		val = invoker.get_value();
		return true;
	}
	catch(const std::exception&)
	{
		return false;
	}
}
} // namespace mono
