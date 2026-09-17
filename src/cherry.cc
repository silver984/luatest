#include <aether/cherry.hh>

#include <fmt/format.h>

namespace aether {

void cherry::banana() {
	apple::banana();
	fmt::println("hello from cherry::banana");
}

} // namespace aether