#ifndef LDDS_GLOBAL_H
#define LDDS_GLOBAL_H
 
#ifdef _WIN32
#ifdef LDDS_LIBRARY
#define LDDS_EXPORT __declspec(dllexport)
#else
#define LDDS_EXPORT __declspec(dllimport)
#endif
#else
#define LDDS_EXPORT __attribute__((visibility("default")))
#endif


#endif //LIDL_GLOBAL_H
