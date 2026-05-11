
#include <TargetConditionals.h>
#if TARGET_OS_IOS == 1 || TARGET_OS_VISION == 1
#import <UIKit/UIKit.h>
#else
#import <Cocoa/Cocoa.h>
#endif

#define IPLUG_AUVIEWCONTROLLER IPlugAUViewController_vCurvessor
#define IPLUG_AUAUDIOUNIT IPlugAUAudioUnit_vCurvessor
#import <CurvessorAU/IPlugAUViewController.h>
#import <CurvessorAU/IPlugAUAudioUnit.h>

//! Project version number for CurvessorAU.
FOUNDATION_EXPORT double CurvessorAUVersionNumber;

//! Project version string for CurvessorAU.
FOUNDATION_EXPORT const unsigned char CurvessorAUVersionString[];

@class IPlugAUViewController_vCurvessor;
