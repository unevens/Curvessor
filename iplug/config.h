#define PLUG_NAME "Curvessor"
#define PLUG_MFR "unevens"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'CrvI'
#define PLUG_MFR_ID 'UnEv'
#define PLUG_URL_STR "https://www.unevens.net"
#define PLUG_EMAIL_STR "hi@unevens.net"
#define PLUG_COPYRIGHT_STR "Copyright Dario Mambro 2020-2026"
#define PLUG_CLASS_NAME Curvessor

#define BUNDLE_NAME "Curvessor"
#define BUNDLE_MFR "unevens"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "Curvessor"

// "2-2"   = stereo in, stereo out (no sidechain)
// "2.2-2" = main stereo + sidechain stereo in, stereo out
#define PLUG_CHANNEL_IO "2-2 2.2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1024
#define PLUG_HEIGHT 640
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1
#define PLUG_MIN_WIDTH 256
#define PLUG_MIN_HEIGHT 256
#define PLUG_MAX_WIDTH 8192
#define PLUG_MAX_HEIGHT 8192

#define AUV2_ENTRY Curvessor_Entry
#define AUV2_ENTRY_STR "Curvessor_Entry"
#define AUV2_FACTORY Curvessor_Factory
#define AUV2_VIEW_CLASS Curvessor_View
#define AUV2_VIEW_CLASS_STR "Curvessor_View"

#define AAX_TYPE_IDS 'ITP1'
#define AAX_TYPE_IDS_AUDIOSUITE 'ITA1'
#define AAX_PLUG_MFR_STR "Acme"
#define AAX_PLUG_NAME_STR "Curvessor\nIPEF"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx|Dynamics"

#define CLAP_MANUAL_URL "https://www.unevens.net"
#define CLAP_SUPPORT_URL "https://www.unevens.net"
#define CLAP_DESCRIPTION "Spline-curve dynamic range processor"
#define CLAP_FEATURES "audio-effect", "compressor"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
