#ifndef LIDL_GLOBAL_H
#define LIDL_GLOBAL_H

#ifdef _WIN32
#ifdef LIDL_LIBRARY
#define LIDL_EXPORT __declspec(dllexport)
#else
#define LIDL_EXPORT __declspec(dllimport)
#endif
#else
#define LIDL_EXPORT __attribute__((visibility("default")))
#endif


#endif // LIDL_GLOBAL_H
