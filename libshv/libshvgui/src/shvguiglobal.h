#pragma once

#  ifdef _WIN32
#    define Q_DECL_EXPORT     __declspec(dllexport)
#    define Q_DECL_IMPORT     __declspec(dllimport)
#  else
#    define Q_DECL_EXPORT     __attribute__((visibility("default")))
#    define Q_DECL_IMPORT     __attribute__((visibility("default")))
#    define Q_DECL_HIDDEN     __attribute__((visibility("hidden")))
#  endif

/// Declaration of macros required for exporting symbols
/// into shared libraries
#if defined(SHVGUI_BUILD_DLL)
#  define SHVGUI_DECL_EXPORT Q_DECL_EXPORT
#else
#  define SHVGUI_DECL_EXPORT Q_DECL_IMPORT
#endif

