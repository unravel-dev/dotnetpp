#include "clr_gc_handle.h"
#include "clr_bridge.h"

namespace clr
{

auto gc_get_heap_size() -> int64_t
{
	return bridge().gc_get_heap_size();
}

auto gc_get_used_size() -> int64_t
{
	return bridge().gc_get_used_size();
}

void gc_collect()
{
	bridge().gc_collect();
}

} // namespace clr
