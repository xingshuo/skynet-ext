
namespace skynet_ext {
namespace fsnotify {

#ifndef PTYPE_FSNOTIFY
#define PTYPE_FSNOTIFY 18 // Notice: must be different with `PTYPE_XXX` defined in "skynet.h"
#endif

enum ErrCode {
	OK = 0,
	INOTIFY_INIT_ERROR = -1,
	SKYNET_SOCKET_BIND_ERROR = -2
};

} // namespace fsnotify
} // namespace skynet_ext