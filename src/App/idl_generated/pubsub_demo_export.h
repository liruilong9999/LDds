#ifndef PUBSUB_DEMO_EXPORT_H
#define PUBSUB_DEMO_EXPORT_H

#ifdef _WIN32
#  ifdef PUBSUB_DEMO_IDL_EXPORTS
#    define PUBSUB_DEMO_IDL_API __declspec(dllexport)
#  else
#    define PUBSUB_DEMO_IDL_API __declspec(dllimport)
#  endif
#else
#  define PUBSUB_DEMO_IDL_API
#endif

#endif // PUBSUB_DEMO_EXPORT_H
