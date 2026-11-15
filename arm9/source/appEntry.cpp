#include <stdint.h>
#include <string.h>

extern "C" int fv_runApplication(int argc, char **argv);
extern "C" uint8_t fv_applicationStack[32768];

int main(int argc, char **argv)
{
    // A known fill lets debugger checks measure the deepest stack use.
    memset(fv_applicationStack, 0xa5, sizeof(fv_applicationStack));
    return fv_runApplication(argc, argv);
}
