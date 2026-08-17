#include <cctype>
#include <cstddef>

extern "C" long nexus_transform(const char *input,
                                 std::size_t input_size,
                                 char *output,
                                 std::size_t output_capacity) noexcept {
  if (input == nullptr || output == nullptr ||
      output_capacity < input_size) {
    return -1;
  }

  for (std::size_t index = 0; index < input_size; ++index) {
    const auto character = static_cast<unsigned char>(input[index]);
    output[index] = static_cast<char>(std::toupper(character));
  }
  return static_cast<long>(input_size);
}
