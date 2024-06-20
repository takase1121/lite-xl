#include <stdio.h>
#import <Foundation/Foundation.h>
#include <lua.h>
#include <objc/runtime.h>

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
BOOL swizzled_applicationSupportsSecureRestorableState(id __unused self, SEL __unused _cmd) {
  puts("applicationSupportsSecureRestorableState called");
    return YES;
}

/* this function uses method swizzling to replace SDLAppDelegate methods on the fly */
void enable_secure_restorable_state() {
  @autoreleasepool {
    SEL applicationSupportsSecureRestorableState = NSSelectorFromString(@"applicationSupportsSecureRestorableState");
    Class cls = objc_getClass("SDLAppDelegate");
    if (!cls) return;
    
    Method method = class_getInstanceMethod(cls, applicationSupportsSecureRestorableState);
    if (!method) {
      // add the method
      class_addMethod(cls, applicationSupportsSecureRestorableState, (IMP) swizzled_applicationSupportsSecureRestorableState, "B@:");
    } else {
      // replace the method
      method_setImplementation(method, (IMP) swizzled_applicationSupportsSecureRestorableState);
    }
  }
}