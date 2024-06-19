#import <Foundation/Foundation.h>
#include <lua.h>

#ifdef MACOS_USE_BUNDLE
void set_macos_bundle_resources(lua_State *L)
{ @autoreleasepool
{
    NSString* resource_path = [[NSBundle mainBundle] resourcePath];
    lua_pushstring(L, [resource_path UTF8String]);
    lua_setglobal(L, "MACOS_RESOURCES");
}}
#endif

/* Thanks to mathewmariani, taken from his lite-macos github repository. */
void enable_momentum_scroll() {
  [[NSUserDefaults standardUserDefaults]
    setBool: YES
    forKey: @"AppleMomentumScrollSupported"];
}

/* https://developer.apple.com/documentation/macos-release-notes/appkit-release-notes-for-macos-12#Restorable-State
 * we don't use any state restoration; setting this to make macOS happy
 * https://github.com/libsdl-org/SDL/pull/6061/files#diff-11d3bf36cd6fea9d46b9a3ca1ff43e9ade6af1330239529efdd2d68174541b5d
 */
@implementation SDLAppDelegate : NSObject
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app {
  return YES;
}
@end
