#ifndef ANDROID_SBS_H
#define ANDROID_SBS_H

namespace android {

#define SBS_FLAG_ENABLED (1 << 0)

class SBS {
 public:
  int flags;
  int zoom;			/* 0-255 */
  int imgdist;
  int isLandscape;
};

};

#endif // ANDROID_SBS_H
