#include "mono_type.h"
#include "mono_assembly.h"
#include "mono_exception.h"

#include "mono_domain.h"
#include "mono_field.h"
#include "mono_method.h"
#include "mono_object.h"
#include "mono_property.h"
#include "mono_type_conversion.h"

BEGIN_MONO_INCLUDE
#include <mono/metadata/appdomain.h>
#include <mono/metadata/attrdefs.h>
#include <mono/metadata/debug-helpers.h>
END_MONO_INCLUDE
#include <iostream>


namespace mono
{
struct mono_type::meta_info
{
	size_t hash = 0;
	std::string name_space;
	std::string name;
	std::string fullname;
	std::uint32_t size = 0;
	std::uint32_t align = 0;
	int rank = 0;
	bool is_valuetype = true;
	bool is_enum = false;
	bool is_array = false;
};

namespace
{

auto get_type_cache() -> std::unordered_map<MonoClass*, std::shared_ptr<mono_type::meta_info>>&
{
	static std::unordered_map<MonoClass*, std::shared_ptr<mono_type::meta_info>> type_cache;
	return type_cache;
}

auto get_meta_info(MonoClass* cls) -> std::shared_ptr<mono_type::meta_info>
{
	auto& cache = get_type_cache();
	auto it = cache.find(cls);
	if(it != cache.end())
	{
		return it->second;
	}
	return nullptr;
}

void set_meta_info(MonoClass* cls, std::shared_ptr<mono_type::meta_info> meta)
{
	auto& cache = get_type_cache();
	cache[cls] = meta;
}

constexpr static uint64_t s_Table64[256] = {

0ull,
	12911341560706588527ull,
	17619267392293085275ull,
	5164075066763771700ull,
	8921845837811637811ull,
	14483170935171449180ull,
	10328150133527543400ull,
	4357999468653093127ull,
	17843691675623275622ull,
	4940391307328217865ull,
	226782375002905661ull,
	12685511915359257426ull,
	10119945210068853333ull,
	4566377562367245626ull,
	8715998937306186254ull,
	14689403211693301089ull,
	9051005139383707209ull,
	14895072503764629798ull,
	9880782614656435730ull,
	4193374422961527165ull,
	453564750005811322ull,
	13070904082541799189ull,
	17496296445768931361ull,
	4747102235666401102ull,
	9960315520700766767ull,
	4113029525020509504ull,
	9132755124734491252ull,
	14812441257301386523ull,
	17431997874612372508ull,
	4811156168024382323ull,
	391483189436228679ull,
	13132671735097031464ull,
	18102010278767414418ull,
	5195199925788447741ull,
	1131375642422963401ull,
	13591081480414639014ull,
	9288535643022529185ull,
	3731739485546663374ull,
	8386748845923054330ull,
	14361410892855143829ull,
	907129500011622644ull,
	13814943346342178715ull,
	17875617253995106479ull,
	5421418680781082560ull,
	8594564625313771207ull,
	14152643483341451688ull,
	9494204471332802204ull,
	3525329033817543155ull,
	9704381199536204507ull,
	3855837706121835956ull,
	8226059050041019008ull,
	13908973417437222383ull,
	18265510249468982504ull,
	5643692520190618503ull,
	718348998302913715ull,
	13463047253836762076ull,
	8146277531524994749ull,
	13989069943491807698ull,
	9622312336048764646ull,
	3938150108875254153ull,
	782966378872457358ull,
	13399312233903888353ull,
	18327840216347633877ull,
	5582173445676054458ull,
	7257036000092981153ull,
	15535280666427316430ull,
	10390399851576895482ull,
	2529986302517213333ull,
	2262751284845926802ull,
	12414353723947190013ull,
	16997392145760156105ull,
	6398650419759490726ull,
	10599130201908394951ull,
	2322133910755632296ull,
	7463478971093326748ull,
	15329644185724306675ull,
	16773497691846108660ull,
	6622864283287239323ull,
	2036569382881248687ull,
	12640783567252986560ull,
	1814259000023245288ull,
	12250853444207230599ull,
	17125426475222188467ull,
	6811676960462675676ull,
	7132938157145702363ull,
	15119434731753103540ull,
	10842837361562165120ull,
	2690676064372932847ull,
	17189129250627542414ull,
	6747026957542163169ull,
	1875814858707893717ull,
	12188560364711551674ull,
	10762704257491731389ull,
	2770420489343360210ull,
	7050658067635086310ull,
	15201536148867841161ull,
	11493583972846619443ull,
	3219832958944941148ull,
	7711675412243671912ull,
	15576564987190227975ull,
	16452118100082038016ull,
	6305011443818121839ull,
	1213047649942025563ull,
	11816267669673208372ull,
	7503259434831574869ull,
	15784731923736995898ull,
	11287385040381237006ull,
	3425713581329221729ull,
	1436697996605827430ull,
	11591809733187859977ull,
	16677985422973077821ull,
	6078267261889762898ull,
	16292555063049989498ull,
	5851447209550246421ull,
	1630020308903038241ull,
	11939238787801010766ull,
	11081681957373440841ull,
	3090674103720225830ull,
	7876300217750508306ull,
	16023932746787097725ull,
	1565932757744914716ull,
	12003503911822413427ull,
	16230825569204842823ull,
	5913566482019610152ull,
	7956607163135676207ull,
	15944361922680361024ull,
	11164346891352108916ull,
	3008957496780927003ull,
	14514072000185962306ull,
	8809633696146542637ull,
	4460922918905818905ull,
	10287960411460399222ull,
	12879331835779764593ull,
	113391187501452830ull,
	5059972605034426666ull,
	17660565739912801861ull,
	4525502569691853604ull,
	10224187249629523019ull,
	14576435430675780479ull,
	8748148222884465680ull,
	4980157760350383383ull,
	17740628527280140920ull,
	12797300839518981452ull,
	195741594718114339ull,
	13040162471224305931ull,
	565687821211481700ull,
	4644267821511264592ull,
	17536326748496696895ull,
	14926957942186653496ull,
	8937808626997553239ull,
	4297282312656885603ull,
	9839608450464401420ull,
	4852190599768102253ull,
	17327666750234135042ull,
	13245728566574478646ull,
	359174499151456857ull,
	4073138765762497374ull,
	10063573324157604913ull,
	14700457781105076997ull,
	9163920108173816938ull,
	3628518000046490576ull,
	9328460452529085631ull,
	14330211790445699979ull,
	8498696072880078052ull,
	5299565100954197475ull,
	18061012165519327884ull,
	13623353920925351352ull,
	1018284691440624343ull,
	14265876314291404726ull,
	8562713237611094233ull,
	3566469078572851181ull,
	9390260331795218562ull,
	13702854325316886917ull,
	937907429353946858ull,
	5381352128745865694ull,
	17978417549248290481ull,
	5746791986423309721ull,
	18225777846762470134ull,
	13494053915084326338ull,
	606523824971012781ull,
	3751629717415787434ull,
	9745292510640121029ull,
	13876787882151992305ull,
	8338992711486538910ull,
	13285957365033343487ull,
	815010154451519120ull,
	5540840978686720420ull,
	18431906428167644875ull,
	14101316135270172620ull,
	8115412784602421411ull,
	3978303581567838103ull,
	9519354766961195256ull,
	12527462061959317731ull,
	2230461459452909452ull,
	6439665917889882296ull,
	16893009583564617687ull,
	15423350824487343824ull,
	7288217715337890239ull,
	2490078880175191691ull,
	10493603952060017124ull,
	6520081235612152965ull,
	16813546994155744234ull,
	12610022887636243678ull,
	2148641156328442801ull,
	2426095299884051126ull,
	10557972909709735385ull,
	15361512820870335213ull,
	7350228890552538498ull,
	15006518869663149738ull,
	7165105895222849989ull,
	2649782550477098737ull,
	10947027550912647582ull,
	12362696414880903321ull,
	1783234539286425590ull,
	6851427162658443458ull,
	17022309211647725485ull,
	2873395993211654860ull,
	10722532847870938531ull,
	15232418832718623383ull,
	6938393941075996152ull,
	6642978682516671743ull,
	17230443782969840528ull,
	12156534523779525796ull,
	1989151790783919051ull,
	6263731030979658865ull,
	16556202624882645790ull,
	11702894419100492842ull,
	1245039440087595845ull,
	3260040617806076482ull,
	11390642587947386157ull,
	15688795063501830681ull,
	7680756410435167606ull,
	11622868312827688983ull,
	1324891275238549368ull,
	6181348207440451660ull,
	16638201170595874595ull,
	15752600435501016612ull,
	7616209416359311691ull,
	3321489341258335871ull,
	11328242235714328848ull,
	3131865515489829432ull,
	10977756817953029463ull,
	16137146508898304611ull,
	7844397531750915340ull,
	5811434156413844491ull,
	16395372229761246052ull,
	11827132964039220304ull,
	1660744670629167935ull,
	15913214326271352414ull,
	8068573254449152305ull,
	2905717078206922245ull,
	11204220263579804010ull,
	12035829987123708013ull,
	1452858539103461122ull,
	6017914993561854006ull,
	16189773752444600153ull};

inline constexpr auto hash_impl(const char* data, size_t size, uint64_t crc) -> uint64_t
{
	for(size_t i = 0; i < size; ++i)
	{
		auto old_crc = crc;
		crc >>= 8;
		crc ^= s_Table64[static_cast<unsigned char>(static_cast<uint64_t>(data[i]) ^ old_crc)];
	}
	return crc;
}
inline constexpr auto crc64(const char* data, size_t size) -> uint64_t
{
	return hash_impl(data, size, 0);
}

inline constexpr auto hash_impl(const char* data, uint64_t crc) -> uint64_t
{
	size_t i = 0;
	while(data[i] != 0)
	{
		auto old_crc = crc;
		crc >>= 8;
		crc ^= s_Table64[static_cast<unsigned char>(static_cast<uint64_t>(data[i]) ^ old_crc)];
		++i;
	}
	return crc;
}

inline constexpr auto crc64(const char* data) -> uint64_t
{
	return hash_impl(data, 0);
}

auto strip_namespace(const std::string& full_name) -> std::string
{
	std::string result;
	size_t start = 0;

	while(start < full_name.size())
	{
		// Find the next '<' or ',' to handle generic arguments
		size_t end = full_name.find_first_of("<,", start);

		// If not found, process the remainder
		if(end == std::string::npos)
		{
			end = full_name.size();
		}

		// Extract the segment (e.g., "Namespace.Type" or "Type")
		std::string segment = full_name.substr(start, end - start);

		// Remove everything before the last '.' (namespace separator)
		size_t last_dot = segment.find_last_of('.');
		if(last_dot != std::string::npos)
		{
			segment = segment.substr(last_dot + 1);
		}

		// Append the processed segment
		result += segment;

		// Add back the delimiter if it's not the end of the string
		if(end < full_name.size() && (full_name[end] == '<' || full_name[end] == ','))
		{
			result += full_name[end];
		}

		// Move to the next segment
		start = end + 1;
	}

	return result;
}

// This function returns a vector of pairs. Each pair is composed of the underlying integer value
// and its corresponding string representation from the enum type.
// 'domain' is the MonoDomain*, and 'enumClass' is the MonoClass* representing the enum type.
template <typename T>
std::vector<std::pair<T, std::string>> get_enum_options(MonoClass* enumClass)
{
	MonoDomain* domain = mono_domain_get();
	std::vector<std::pair<T, std::string>> options;
	if(!domain || !enumClass)
		return options; // Return an empty vector if invalid parameters.

	// Get the MonoType for the enum.
	MonoType* enumType = mono_class_get_type(enumClass);
	if(!enumType)
		return options;

	// Convert the MonoType into a reflection type (System.Type).
	MonoReflectionType* reflectionType = mono_type_get_object(domain, enumType);
	if(!reflectionType)
		return options;

	// Get the System.Enum class via the Mono API.
	MonoClass* systemEnumClass = mono_get_enum_class();
	if(!systemEnumClass)
		return options;

	// Locate the 'GetValues' method defined on System.Enum.
	// The expected signature is: public static Array GetValues(Type enumType)
	MonoMethod* getValuesMethod = mono_class_get_method_from_name(systemEnumClass, "GetValues", 1);
	if(!getValuesMethod)
		return options;

	// Prepare the argument: the reflection type (a System.Type) for our enum.
	void* args[1] = {reflectionType};

	// Invoke the method to retrieve the array of enum values.
	MonoObject* result = mono_runtime_invoke(getValuesMethod, nullptr, args, nullptr);
	if(!result)
		return options;

	// Convert the result to a MonoArray.
	MonoArray* enumArray = (MonoArray*)result;
	uintptr_t arrayLength = mono_array_length(enumArray);

	for(uintptr_t i = 0; i < arrayLength; i++)
	{
		// Retrieve the enum value object.
		T value = mono_array_get(enumArray, T, i);

		MonoObject* enumValueObj = mono_value_box(domain, enumClass, &value);

		// Get the string representation of the enum value by calling its ToString() method.
		MonoString* strObj = (MonoString*)mono_object_to_string(enumValueObj, nullptr);
		char* utf8Str = mono_string_to_utf8(strObj);
		std::string name(utf8Str);
		mono_free(utf8Str);

		options.push_back(std::make_pair(value, name));
	}

	return options;
}

const char* get_owning_namespace(MonoClass* klass)
{
    if (!klass) return "";

    // Walk up to the top-level declaring type
    MonoClass* cur = klass;
    while (MonoClass* parent = mono_class_get_nesting_type(cur))
        cur = parent;

    const char* ns = mono_class_get_namespace(cur);
    return ns ? ns : "";
}
} // namespace
mono_type::mono_type() = default;

mono_type::mono_type(MonoImage* image, const std::string& name)
	: mono_type(image, "", name)
{
}

mono_type::mono_type(MonoImage* image, const std::string& name_space, const std::string& name)
	: mono_type(mono_class_from_name(image, name_space.c_str(), name.c_str()))
{
}

mono_type::mono_type(MonoClass* cls)
{
	if(cls)
	{
		class_ = cls;
		generate_meta();
	}
}
mono_type::mono_type(MonoType* type)
{
	if(type)
	{
		class_ = mono_class_from_mono_type(type);

		if(class_)
		{
			generate_meta();
		}
	}
}
auto mono_type::valid() const -> bool
{
	return class_ != nullptr;
}

auto mono_type::new_instance() const -> mono_object
{
	const auto& domain = mono_domain::get_current_domain();
	return new_instance(domain);
}

auto mono_type::new_instance(const mono_domain& domain) const -> mono_object
{
	return mono_object(domain, *this);
}

auto mono_type::get_method(const std::string& name_with_args) const -> mono_method
{
	return mono_method(*this, name_with_args);
}

auto mono_type::get_method(const std::string& name, int argc) const -> mono_method
{
	return mono_method(*this, name, argc);
}

auto mono_type::get_field(const std::string& name) const -> mono_field
{
	return mono_field(*this, name);
}

auto mono_type::get_property(const std::string& name) const -> mono_property
{
	return mono_property(*this, name);
}

auto mono_type::get_fields(bool include_base) const -> std::vector<mono_field>
{
	std::vector<mono_field> fields;
	std::vector<MonoClass*> hierarchy;
	auto class_ptr = class_;
	while(class_ptr)
	{
		hierarchy.push_back(class_ptr);
		if(!include_base)
		{
			break;
		}
		class_ptr = mono_class_get_parent(class_ptr);
	}
	for(auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it)
	{
		void* iter = nullptr;
		auto field = mono_class_get_fields(*it, &iter);
		while(field)
		{
			std::string name = mono_field_get_name(field);
			fields.emplace_back(get_field(name));
			field = mono_class_get_fields(*it, &iter);
		}
	}
	return fields;
}

auto mono_type::get_properties(bool include_base) const -> std::vector<mono_property>
{
	std::vector<mono_property> props;

	std::vector<MonoClass*> hierarchy;
	auto class_ptr = class_;
	while(class_ptr)
	{
		hierarchy.push_back(class_ptr);
		if(!include_base)
		{
			break;
		}
		class_ptr = mono_class_get_parent(class_ptr);
	}
	for(auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it)
	{
		void* iter = nullptr;
		auto prop = mono_class_get_properties(*it, &iter);
		while(prop)
		{
			std::string name = mono_property_get_name(prop);
			props.emplace_back(get_property(name));
			prop = mono_class_get_properties(*it, &iter);
		}
	}
	return props;
}

auto mono_type::get_methods(bool include_base) const -> std::vector<mono_method>
{
	std::vector<mono_method> methods;
	std::vector<MonoClass*> hierarchy;
	auto class_ptr = class_;
	while(class_ptr)
	{
		hierarchy.push_back(class_ptr);
		if(!include_base)
		{
			break;
		}
		class_ptr = mono_class_get_parent(class_ptr);
	}
	for(auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it)
	{
		void* iter = nullptr;
		auto method = mono_class_get_methods(*it, &iter);
		while(method != nullptr)
		{
			auto sig = mono_method_signature(method);
			char* mono_signature = mono_signature_get_desc(sig, false);
			std::string signature(mono_signature);
			mono_free(mono_signature);
			std::string name = mono_method_get_name(method);
			std::string fullname = name + "(" + signature + ")";
			methods.emplace_back(get_method(fullname));
			method = mono_class_get_methods(*it, &iter);
		}
	}
	return methods;
}

auto mono_type::get_attributes(bool include_base) const -> std::vector<mono_object>
{
	std::vector<mono_object> result;
	std::vector<MonoClass*> hierarchy;
	auto class_ptr = class_;
	while(class_ptr)
	{
		hierarchy.push_back(class_ptr);
		if(!include_base)
		{
			break;
		}
		class_ptr = mono_class_get_parent(class_ptr);
	}
	for(auto it = hierarchy.rbegin(); it != hierarchy.rend(); ++it)
	{
		MonoCustomAttrInfo* attr_info = mono_custom_attrs_from_class(*it);
		if(attr_info)
		{
			for(int i = 0; i < attr_info->num_attrs; ++i)
			{
				MonoCustomAttrEntry* entry = &attr_info->attrs[i];
				MonoClass* attr_class = mono_method_get_class(entry->ctor);
				if(attr_class)
				{
					MonoObject* attr_obj = mono_custom_attrs_get_attr(attr_info, attr_class);
					if(attr_obj)
					{
						result.emplace_back(attr_obj);
					}
				}
			}
			mono_custom_attrs_free(attr_info);
		}
	}
	return result;
}

auto mono_type::has_base_type() const -> bool
{
	return mono_class_get_parent(class_) != nullptr;
}

auto mono_type::get_base_type() const -> mono_type
{
	auto base = mono_class_get_parent(class_);
	return mono_type(base);
}

void mono_type::gather_nested_types_recursive(std::vector<mono_type>& nested_types) const
{
	void* iter = nullptr;
	auto nested = mono_class_get_nested_types(class_, &iter);
	while(nested)
	{
		mono_type nested_type(nested);
		nested_types.emplace_back(nested_type);
		nested_type.gather_nested_types_recursive(nested_types);
		nested = mono_class_get_nested_types(class_, &iter);
	}
}

auto mono_type::get_nesting_type() const -> mono_type
{
	return mono_type(mono_class_get_nesting_type(class_));
}

auto mono_type::get_nested_types() const -> std::vector<mono_type>
{
	std::vector<mono_type> nested_types;
	gather_nested_types_recursive(nested_types);
	return nested_types;
}

auto mono_type::equals(const mono_type& other) const -> bool
{
	return get_internal_ptr() == other.get_internal_ptr();
}

auto mono_type::get_internal_ptr() const -> MonoClass*
{
	return class_;
}

void mono_type::generate_meta()
{
	auto meta = get_meta_info(class_);
	if(!meta)
	{
		meta = std::make_shared<meta_info>();
		meta->hash = get_hash();
		meta->name_space = get_namespace();
		meta->name = get_name();
		meta->fullname = get_fullname();
		meta->rank = get_rank();
		meta->is_valuetype = is_valuetype();
		meta->is_enum = is_enum();
		meta->size = get_sizeof();
		meta->align = get_alignof();
		meta->is_array = is_array();
		set_meta_info(class_, meta);
	}

	meta_ = meta;
}

auto mono_type::is_derived_from(const mono_type& type) const -> bool
{
	return mono_class_is_subclass_of(class_, type.get_internal_ptr(), true) != 0;
}
auto mono_type::get_namespace() const -> std::string
{
	if(meta_)
	{
		return meta_->name_space;
	}
	return get_owning_namespace(class_);
}
auto mono_type::get_name() const -> std::string
{
	return get_name(false);
}

auto mono_type::get_hash(const std::string& name) -> size_t
{
	return crc64(name.data(), name.size());
}
auto mono_type::get_hash(const char* name) -> size_t
{
	return crc64(name);
}

auto mono_type::get_hash() const -> size_t
{
	if(!class_)
	{
		return 0;
	}
	if(meta_)
	{
		return meta_->hash;
	}
	MonoType* type = mono_class_get_type(class_);
	char* name = mono_type_get_name(type);
	size_t hash = get_hash(name);
	mono_free(name);
	return hash;
}

auto mono_type::get_name(bool full) const -> std::string
{
	if(meta_)
	{
		return meta_->name;
	}
	MonoType* type = mono_class_get_type(class_);
	if(full)
	{
		char* mono_name = mono_type_get_name(type);
		std::string name(mono_name);
		mono_free(mono_name);
		return name;
	}

	if(mono_type_get_type(type) != MONO_TYPE_GENERICINST)
	{
		return mono_class_get_name(class_);
	}
	// Get generic arguments as part of the type name
	char* mono_name = mono_type_get_name(type);
	std::string name(mono_name);
	mono_free(mono_name);
	return strip_namespace(name);
}

auto mono_type::get_fullname() const -> std::string
{
	if(meta_)
	{
		return meta_->fullname;
	}
	return get_name(true);
}
auto mono_type::is_valuetype() const -> bool
{
	if(meta_)
	{
		return meta_->is_valuetype;
	}
	return !!mono_class_is_valuetype(class_);
}

auto mono_type::mono_type::is_enum() const -> bool
{
	if(meta_)
	{
		return meta_->is_enum;
	}
	return mono_class_is_enum(class_);
}

auto mono_type::get_enum_base_type() const -> mono_type
{
	return mono_type(mono_class_enum_basetype(class_));
}

template<>
auto mono_type::get_enum_values<uint8_t>() const -> std::vector<std::pair<uint8_t, std::string>>
{
	return get_enum_options<uint8_t>(class_);
}

template<>
auto mono_type::get_enum_values<uint16_t>() const -> std::vector<std::pair<uint16_t, std::string>>
{
	return get_enum_options<uint16_t>(class_);
}

template<>
auto mono_type::get_enum_values<uint32_t>() const -> std::vector<std::pair<uint32_t, std::string>>
{
	return get_enum_options<uint32_t>(class_);
}

template<>
auto mono_type::get_enum_values<uint64_t>() const -> std::vector<std::pair<uint64_t, std::string>>
{
	return get_enum_options<uint64_t>(class_);
}

template<>
auto mono_type::get_enum_values<int8_t>() const -> std::vector<std::pair<int8_t, std::string>>
{
	return get_enum_options<int8_t>(class_);
}

template<>
auto mono_type::get_enum_values<int16_t>() const -> std::vector<std::pair<int16_t, std::string>>
{
	return get_enum_options<int16_t>(class_);
}

template<>
auto mono_type::get_enum_values<int32_t>() const -> std::vector<std::pair<int32_t, std::string>>
{
	return get_enum_options<int32_t>(class_);
}

template<>
auto mono_type::get_enum_values<int64_t>() const -> std::vector<std::pair<int64_t, std::string>>
{
	return get_enum_options<int64_t>(class_);
}

auto mono_type::is_class() const -> bool
{
	return !is_valuetype();
}

auto mono_type::is_struct() const -> bool
{
	return is_valuetype() && !is_enum();
}

auto mono_type::get_rank() const -> int
{
	if(meta_)
	{
		return meta_->rank;
	}
	return mono_class_get_rank(class_);
}

auto mono_type::is_array() const -> bool
{
	if(meta_)
	{
		return meta_->is_array;
	}
	if (!class_)
	{
		return false;
	}
	return mono_class_get_rank(class_) > 0;
}

auto mono_type::get_element_type() const -> mono_type
{
	if (!class_)
	{
		return {};
	}
	
	if(is_array())
	{
		return get_array_element_type();
	}
	if(is_list())
	{
		return get_list_element_type();
	}
	return {};
}

auto mono_type::get_array_element_type() const -> mono_type
{	
	MonoClass* element_class = mono_class_get_element_class(class_);
	return mono_type(element_class);
}
auto mono_type::get_list_element_type() const -> mono_type
{
	mono_property item_prop = get_property("Item");
	if(!item_prop.is_valid())
		return {};

	// The return type of the "Item" property is the element type T
	return item_prop.get_type();
}

auto mono_type::get_sizeof() const -> uint32_t
{
	if(meta_)
	{
		return meta_->size;
	}
	uint32_t align{};
	return std::uint32_t(mono_class_value_size(class_, &align));
}

auto mono_type::get_alignof() const -> uint32_t
{
	if(meta_)
	{
		return meta_->align;
	}
	uint32_t align{};
	mono_class_value_size(class_, &align);
	return align;
}

auto mono_type::is_abstract() const -> bool
{
	return (mono_class_get_flags(class_) & MONO_TYPE_ATTR_ABSTRACT) != 0;
}

auto mono_type::is_sealed() const -> bool
{
	return (mono_class_get_flags(class_) & MONO_TYPE_ATTR_SEALED) != 0;
}

auto mono_type::is_interface() const -> bool
{
	return (mono_class_get_flags(class_) & MONO_TYPE_ATTR_INTERFACE) != 0;
}

auto mono_type::is_serializable() const -> bool
{
	return (mono_class_get_flags(class_) & MONO_TYPE_ATTR_SERIALIZABLE) != 0;
}

auto mono_type::is_string() const -> bool
{
	return class_ == mono_get_string_class();
}

auto mono_type::is_list() const -> bool
{
	// Check if it's a generic List<T>
	auto fullname = get_fullname();
	return fullname.find("System.Collections.Generic.List<") == 0;
}
void reset_type_cache()
{
	auto& cache = get_type_cache();
	cache.clear();
}
} // namespace mono
