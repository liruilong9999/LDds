#ifndef LDDS_GLOBAL_H
#define LDDS_GLOBAL_H

#ifdef LDDSCORE_LIBRARY
#define LDDSCORE_EXPORT __declspec(dllexport)
#else
#define LDDSCORE_EXPORT __declspec(dllimport)
#endif
#endif // !LDDS_GLOBAL_H
