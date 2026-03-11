#ifndef FILE2_EXPORT_H
#define FILE2_EXPORT_H

#ifdef _WIN32
#  ifdef FILE2_IDL_EXPORTS
#    define FILE2_IDL_API __declspec(dllexport)
#  else
#    define FILE2_IDL_API __declspec(dllimport)
#  endif
#else
#  define FILE2_IDL_API
#endif

#endif // FILE2_EXPORT_H
