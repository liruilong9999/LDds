#ifndef LDDSCORE_GLOBAL_H
#define LDDSCORE_GLOBAL_H

#ifdef _WIN32
#ifdef LDDSCORE_LIBRARY
#define LDDSCORE_EXPORT __declspec(dllexport)
#else
#define LDDSCORE_EXPORT __declspec(dllimport)
#endif
#else
#define LDDSCORE_EXPORT __attribute__((visibility("default")))
#endif


#endif // COMMUNATEMODULE_GLOBAL_H
