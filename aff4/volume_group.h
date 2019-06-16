#ifndef     AFF4_VOLUME_GROUP_H_
#define     AFF4_VOLUME_GROUP_H_

#include <unordered_map>
#include <set>

#include "aff4/aff4_io.h"
#include "aff4/data_store.h"

namespace aff4 {

 class VolumeGroup {
 protected:
    std::unordered_map<URN, AFF4Flusher<AFF4Volume>> volume_objs;
    DataStore *resolver;

    std::set<std::string> searchPaths;
    std::unordered_map<URN, std::string> foundVolumes;

    bool FoundVolumesContains(const std::string& filename);

 public:
     VolumeGroup(DataStore *resolver) : resolver(resolver) {}
     virtual ~VolumeGroup() {}

     void AddVolume(AFF4Flusher<AFF4Volume> &&volume);

     AFF4Status GetStream(URN segment_urn, AFF4Flusher<AFF4Stream> &result);
     
     void AddSearchPath(std::string path);
     void RemoveSearchPath(std::string path);
     
     AFF4Status LocateAndAdd(URN& urn);
     void ScanForAFF4Volumes(const std::string& path);
     
 };

} // namespace aff4

#endif  // AFF4_VOLUME_GROUP_H_
