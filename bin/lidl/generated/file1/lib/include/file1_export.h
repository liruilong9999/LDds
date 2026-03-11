#ifndef FILE1_EXPORT_H
#define FILE1_EXPORT_H

#ifdef _WIN32
#  ifdef FILE1_IDL_EXPORTS
#    define FILE1_IDL_API __declspec(dllexport)
#  else
#    define FILE1_IDL_API __declspec(dllimport)
#  endif
#else
#  define FILE1_IDL_API
#endif

#endif // FILE1_EXPORT_H
