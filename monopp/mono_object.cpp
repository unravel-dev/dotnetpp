#include "mono_object.h"
#include "mono_domain.h"
#include "mono_string.h"

namespace mono
{

mono_object::mono_object()
	: object_(nullptr)
{
}

mono_object::mono_object(MonoObject* object)
	: object_(object)
{
	if(object_)
	{
		type_ = mono_type(mono_object_get_class(object));
	}
}

mono_object::mono_object(MonoObject* object, const mono_type& type)
	: type_(type)
    , object_(object)
{
}

mono_object::mono_object(const mono_domain& domain, const mono_type& type)
	: type_(type)
{
	MonoClass* klass = type.get_internal_ptr();

    if (mono_class_is_valuetype(klass))
    {
        // Allocate memory for the raw value on the stack
        int size = mono_class_value_size(klass, nullptr);
        void* buffer = alloca(size);
        memset(buffer, 0, size);

        // Box the value into a MonoObject
        object_ = mono_value_box(domain.get_internal_ptr(), klass, buffer);
    }
    else if(type_.is_string())
    {
        auto mstring = domain.new_string("");
        object_ = mstring.get_internal_ptr();
    }
    else
    {
        // Normal reference type object
        object_ = mono_object_new(domain.get_internal_ptr(), klass);
        mono_runtime_object_init(object_);
    }
}
auto mono_object::get_type() const -> const mono_type&
{
	return type_;
}

auto mono_object::valid() const -> bool
{
	return object_ != nullptr;
}

mono_object::operator bool() const
{
	return valid();
}

auto mono_object::is_valid_mono_object() const -> bool
{
	if(!object_)
	{
		return false;
	}
	return mono_object_get_class(object_) != nullptr;
}

auto mono_object::equals(const mono_object& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_object::get_internal_ptr() const -> MonoObject*
{
	return object_;
}


} // namespace mono
