#import "TinaAppDelegate.h"
#import "TinaViewController.h"

@implementation TinaAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[TinaViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}

@end
