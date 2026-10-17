#include <WeaselConstants.h>
#include <string_view>
static_assert(std::string_view(WEASEL_VERSION) == "0.17.4");
static_assert(std::string_view(WEASEL_PRODUCT_VERSION) == "0.17.4.999.test",
              "Rime distribution version must include build/hash");
int main() { return 0; }
