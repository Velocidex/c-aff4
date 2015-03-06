/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/
#ifndef _AFF4_IMAGER_UTILS_H
#define _AFF4_IMAGER_UTILS_H

#include "aff4_errors.h"
#include "data_store.h"
#include "libaff4.h"
#include "rdf.h"
#include "tclap_parsers.h"

// Supports all integer inputs given as hex.
#define TCLAP_SETBASE_ZERO 1
#include <tclap/CmdLine.h>


/**
 * A Convenience method to add a vector of input URNs to a volume created on an
 * output URN.

 * This is essentially the same as the -i option of the basic imager.
 *
 * @param resolver: A resolver to use.
 * @param input_urns: A vector of URNs to be copied to the output volume.
 * @param output_urn: A URN to create or append the volume to.
 * @param truncate: Should the output file be truncated?
 * @param buffer_size: Inputs will be copied in this buffer size.
 *
 * @return STATUS_OK if images were added successfully.
 */
AFF4Status ImageStream(DataStore &resolver, vector<URN> &input_urns,
                       URN output_urn,
                       bool truncate = true,
                       size_t buffer_size=1024*1024);

/**
 * Copy a stream from a loaded volume into and output stream.
 *
 * @param resolver
 * @param input_urn
 * @param output_urn
 * @param buffer_size
 *
 * @return
 */
AFF4Status ExtractStream(DataStore &resolver, URN input_urn,
                         URN output_urn,
                         bool truncate = true,
                         size_t buffer_size=1024*1024);

class BasicImager {
protected:
  MemoryDataStore resolver;
  URN volume_URN;

  URN output_volume_urn;

  // Type of compression we should use.
  AFF4_IMAGE_COMPRESSION_ENUM compression = AFF4_IMAGE_COMPRESSION_ENUM_ZLIB;

  bool should_abort = false;

  virtual string GetName() {
    return "AFF4 Imager";
  };

  virtual string GetVersion() {
    return AFF4_VERSION;
  };

  AFF4Status GetOutputVolumeURN(URN &volume_urn);

  std::unique_ptr<TCLAP::CmdLine> cmd;

  virtual AFF4Status handle_Verbose();
  virtual AFF4Status handle_aff4_volumes();
  virtual AFF4Status handle_view();
  virtual AFF4Status handle_input();
  virtual AFF4Status handle_export();
  virtual AFF4Status handle_compression();

  // Dispatch handlers based on the parsed config options.
  virtual AFF4Status HandlerDispatch();

  // We use a list here to preserve insertion order.
  std::list<std::unique_ptr<TCLAP::Arg>> args;

  void AddArg(TCLAP::Arg *arg) {
    args.push_back(std::unique_ptr<TCLAP::Arg>(arg));
  };

  TCLAP::Arg *Get(string name) {
    for(auto it=args.begin(); it!=args.end(); it++) {
      if((*it)->getName() == name) {
        return it->get();
      };
    };

    LOG(FATAL) << "Parameter " << name << " not known";
  };

  template<typename T>
  T *GetArg(string name) {
    return dynamic_cast<T *>(Get(name));
  };

public:
  /**
   * This should be overloaded for imagers that need to do something before they
   * start. The method is called during the imager's initialization routine.

   * @return If this returns anything other that STATUS_OK we abort.
   */
  virtual AFF4Status Initialize() {
    return STATUS_OK;
  };

  virtual AFF4Status RegisterArgs() {
    AddArg(new TCLAP::SwitchArg("V", "view", "View AFF4 metadata", false));
    AddArg(new TCLAP::SwitchArg("v", "verbose", "Display more verbose logging",
                            false));

    AddArg(new TCLAP::SwitchArg(
        "t", "truncate", "Truncate the output file. Normally volumes and "
        "images are appended to existing files, but this flag forces the "
        "output file to be truncated first.",
        false));

    AddArg(new TCLAP::MultiArgToNextFlag<string>(
        "i", "input", "File to image. If specified we copy this file to the "
        "output volume located at --output. If there is no AFF4 volume on "
        "--output yet, we create a new volume on it.\n"
        "This can be specified multiple times with shell expansion. e.g.:\n"
        "-i /bin/*",
        false, "/path/to/file/or/device"));

    AddArg(new TCLAP::ValueArg<string>(
        "e", "export", "Name of the stream to export. If specified we try "
        "to open this stream and write it to the --output file. Note that "
        "you will also need to specify an AFF4 volume path to load so we know "
        "where to find the stream. Specifying a relative URN "
        "implies a stream residing in a loaded volume. E.g.\n"

        " -e /dev/sda -o /tmp/myfile my_volume.aff4",
        false, "", "string"));

    AddArg(new TCLAP::ValueArg<string>(
        "o", "output", "Output file to write to. If the file does not "
        "exist we create it.", false, "",
        "/path/to/file"));

    AddArg(new TCLAP::ValueArg<string>(
        "c", "compression", "Type of compression to use (default zlib).",
        false, "", "zlib, snappy, none"));

    AddArg(new TCLAP::UnlabeledMultiArg<string>(
        "aff4_volumes",
        "These AFF4 Volumes will be loaded and their metadata will "
        "be parsed before the program runs.\n"
        "Note that this is necessary before you can extract streams with the "
        "--export flag.",
        false, "/path/to/aff4/volume"));

    return STATUS_OK;
  };


  AFF4Status ParseArgs(int argc, char** argv);

  virtual bool progress_renderer(
      aff4_off_t readptr, ProgressContext &context);

  void Abort();
};


#endif // _AFF4_IMAGER_UTILS_H
