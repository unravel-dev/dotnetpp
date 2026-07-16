#include "mono_assembly.h"
#include "mono_domain.h"
#include "mono_exception.h"

#include "mono_string.h"
#include "mono_type.h"
#include <fstream>
#include <sstream>

BEGIN_MONO_INCLUDE
#include <mono/metadata/appdomain.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/image.h>
#include <mono/metadata/tokentype.h>
END_MONO_INCLUDE

namespace mono
{

namespace
{
template <typename Container = std::string, typename CharT = char, typename Traits = std::char_traits<char>>
auto read_stream_into_container(std::basic_istream<CharT, Traits>& in, Container& container) -> bool
{
	static_assert(
		std::is_same<Container,
					 std::basic_string<CharT, Traits, typename Container::allocator_type>>::value ||
			std::is_same<Container, std::vector<CharT, typename Container::allocator_type>>::value ||
			std::is_same<Container, std::vector<std::make_unsigned_t<CharT>,
												typename Container::allocator_type>>::value ||
			std::is_same<Container,
						 std::vector<std::make_signed_t<CharT>, typename Container::allocator_type>>::value,
		"only strings and vectors of ((un)signed) CharT allowed");

	auto const start_pos = in.tellg();
	if(std::streamsize(-1) == start_pos || !in.good())
	{
		return false;
	}

	if(!in.seekg(0, std::ios_base::end) || !in.good())
	{
		return false;
	}

	auto const end_pos = in.tellg();

	if(std::streamsize(-1) == end_pos || !in.good())
	{
		return false;
	}

	auto const char_count = end_pos - start_pos;

	if(!in.seekg(start_pos) || !in.good())
	{
		return false;
	}

	container.resize(static_cast<std::size_t>(char_count));

	if(!container.empty())
	{
		in.read(reinterpret_cast<CharT*>(&container[0]), char_count);
		container.resize(in.gcount());
	}

	return in.good() || in.eof();
}

namespace
{
static auto split_tokens(const std::string& s) -> std::vector<std::string>
{
    // allow both '.' and '+' as separators in input
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size())
    {
        size_t sep = s.find_first_of(".+", start);
        if (sep == std::string::npos)
        {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, sep - start));
        start = sep + 1;
    }
    return out;
}

static MonoClass* find_direct_nested(MonoClass* outer, const std::string& nested_name)
{
    if (!outer) return nullptr;
    mono_class_init(outer);

    void* iter = nullptr;
    while (MonoClass* nested = mono_class_get_nested_types(outer, &iter))
    {
        const char* n = mono_class_get_name(nested);
        if (n && nested_name == n)
            return nested;
    }
    return nullptr;
}

static MonoClass* class_from_fullname_guessing(MonoImage* image, const std::string& full)
{
    if (!image || full.empty()) return nullptr;

    auto parts = split_tokens(full);
    if (parts.empty()) return nullptr;

    // Try all possible splits:
    // namespace = parts[0..outer_idx-1], outer = parts[outer_idx], nested = parts[outer_idx+1..]
    for (size_t outer_idx = 0; outer_idx < parts.size(); ++outer_idx)
    {
        // Build namespace candidate
        std::string ns;
        for (size_t i = 0; i < outer_idx; ++i)
        {
            if (i) ns.push_back('.');
            ns.append(parts[i].data(), parts[i].size());
        }

        std::string outer(parts[outer_idx].data(), parts[outer_idx].size());
        if (outer.empty())
            continue;

        MonoClass* klass = mono_class_from_name(image, ns.c_str(), outer.c_str());
        if (!klass)
            continue;

        bool ok = true;
        for (size_t ni = outer_idx + 1; ni < parts.size(); ++ni)
        {
            klass = find_direct_nested(klass, parts[ni]);
            if (!klass)
            {
                ok = false;
                break;
            }
        }

        if (ok)
            return klass;
    }

    return nullptr;
}
} // namespace

} // namespace

mono_assembly::mono_assembly(MonoImage* image)
	: image_(image)
{
}

mono_assembly::mono_assembly(const mono_domain& domain, const std::string& path, bool shared)
{
	if(shared)
	{
		assembly_ = mono_domain_assembly_open(domain.get_internal_ptr(), path.c_str());

		if(!assembly_)
			throw mono_exception("NATIVE::Could not open assembly with path : " + path);

		image_ = mono_assembly_get_image(assembly_);
	}
	else
	{
		mono_domain_set(domain.get_internal_ptr(), true);

		// Read the file into memory
		std::ifstream file(path, std::ios::binary); // | std::ios::ate);
		if(!file.is_open())
		{
			throw mono_exception("NATIVE::Could not open assembly with path : " + path);
		}
		std::vector<char> buffer;
		if(!read_stream_into_container(file, buffer))
		{
			throw mono_exception("NATIVE::Could not read assembly with path : " + path);
		}

		// Load the assembly from memory
		MonoImageOpenStatus status;
		image_ =
			mono_image_open_from_data(buffer.data(), static_cast<uint32_t>(buffer.size()), true, &status);
		if(!image_ || status != MONO_IMAGE_OK)
		{
			throw mono_exception("NATIVE::Failed to load assembly from memory with path : " + path);
			return;
		}

		assembly_ = mono_assembly_load_from(image_, path.c_str(), &status);

		if(!assembly_)
			throw mono_exception("NATIVE::Could not open assembly with path : " + path);
	}
}

auto mono_assembly::get_type(const std::string& full_or_simple_name) const -> mono_type
{
	// Fast path: old behavior for non-nested: try as (namespace="", name=simple)
	{
		mono_type t(image_, "", full_or_simple_name);
		if (t.valid())
			return t;
	}

	// If it contains '.' or '+', it might be namespaced or nested
	if (full_or_simple_name.find('.') != std::string::npos ||
		full_or_simple_name.find('+') != std::string::npos)
	{
		if (MonoClass* cls = class_from_fullname_guessing(image_, full_or_simple_name))
			return mono_type(cls);
	}

	// Fallback: maybe it's a non-nested full name "Ns1.Ns2.Type"
	// Try splitting into (namespace, name) at last '.'
	const size_t lastDot = full_or_simple_name.find_last_of('.');
	if (lastDot != std::string::npos)
	{
		std::string ns = full_or_simple_name.substr(0, lastDot);
		std::string n  = full_or_simple_name.substr(lastDot + 1);
		mono_type t(image_, ns, n);
		if (t.valid())
			return t;
	}

	return {};
}

auto mono_assembly::get_type(const std::string& name_space, const std::string& name) const -> mono_type
{
	// If it's a plain name, keep old behavior
	if (name.find('.') == std::string::npos && name.find('+') == std::string::npos)
	{
		return mono_type(image_, name_space, name);
	}

    // Otherwise, resolve "name_space + '.' + name" as a full name
    std::string full = name_space;
    if (!full.empty()) 
	{
		full.push_back('.');
	}
    full += name;

    if (MonoClass* cls = class_from_fullname_guessing(image_, full))
	{
		return mono_type(cls);
	}

	return {};
}

auto mono_assembly::get_corlib() -> mono_assembly
{
	return mono_assembly(mono_get_corlib());
}

std::vector<mono_type> mono_assembly::get_types() const
{
	// Get all types in an assembly
	std::vector<mono_type> classes;

	int num_types = mono_image_get_table_rows(image_, MONO_TABLE_TYPEDEF);

	for(int i = 1; i <= num_types; ++i)
	{
		MonoClass* klass = mono_class_get(image_, (i + MONO_TOKEN_TYPE_DEF));
		if(klass)
		{
			classes.push_back(mono_type(klass));
		}
	}
	return classes;
}

auto mono_assembly::get_types_derived_from(const mono_type& base) const -> std::vector<mono_type>
{
	auto types = get_types();

	std::vector<mono_type> result;

	for(const auto& type : types)
	{
		if(type.is_derived_from(base))
		{
			result.emplace_back(type);
		}
	}

	return result;
}

auto mono_assembly::dump_references() const -> std::vector<std::string>
{
	std::vector<std::string> refs;
	/* Get a pointer to the AssemblyRef metadata table */
	auto table_info = mono_image_get_table_info(image_, MONO_TABLE_ASSEMBLYREF);

	/* Fetch the number of rows available in the table */
	int rows = mono_table_info_get_rows(table_info);

	refs.reserve(size_t(rows));
	/* For each row, print some of its values */
	for(int i = 0; i < rows; i++)
	{
		/* Space where we extract one row from the metadata table */
		uint32_t cols[MONO_ASSEMBLYREF_SIZE];

		/* Extract the row into the array cols */
		mono_metadata_decode_row(table_info, i, cols, MONO_ASSEMBLYREF_SIZE);

		std::stringstream s;
		s << i + 1;
		s << " Version=";
		s << cols[MONO_ASSEMBLYREF_MAJOR_VERSION];
		s << ".";
		s << cols[MONO_ASSEMBLYREF_MINOR_VERSION];
		s << ".";
		s << cols[MONO_ASSEMBLYREF_BUILD_NUMBER];
		s << ".";
		s << cols[MONO_ASSEMBLYREF_REV_NUMBER];
		s << "\n\tName=";
		s << mono_metadata_string_heap(image_, cols[MONO_ASSEMBLYREF_NAME]);

		refs.emplace_back(s.str());
	}

	return refs;
}

auto mono_assembly::equals(const mono_assembly& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_assembly::get_internal_ptr() const -> MonoImage*
{
	return image_;
}

} // namespace mono
