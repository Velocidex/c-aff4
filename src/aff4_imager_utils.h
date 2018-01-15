/*
Copyright 2015 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/
#ifndef SRC_AFF4_IMAGER_UTILS_H
#define SRC_AFF4_IMAGER_UTILS_H

#include "config.h"

#include "aff4_errors.h"
#include "data_store.h"
#include "libaff4.h"
#include "rdf.h"
#include "tclap_parsers.h"

// Supports all integer inputs given as hex.
#define TCLAP_SETBASE_ZERO 1
#include <tclap/CmdLine.h>
#include <set>
#include <list>

namespace aff4 {


/**
 * A Convenience method to add a vector of input URNs to a volume created on an
 * output URN.

 * This is essentially the same as the -i option of the basic imager.
 *
 * @param resolver: A resolver to use.
 * @param input_urns: A vector of URNs to be copied to the output volume.
 * @param output_urn: A URN to create or append the volume to.
 * @param truncate: Should the output file be truncated?
 *
 * @return STATUS_OK if images were added successfully.
 */
AFF4Status ImageStream(DataStore& resolver, std::vector<URN>& input_urns,
                       URN output_urn,
                       bool truncate = true);

/**
 * Copy a stream from a loaded volume into and output stream.
 *
 * @param resolver
 * @param input_urn
 * @param output_urn
 *
 * @return
 */
AFF4Status ExtractStream(DataStore& resolver, URN input_urn,
                         URN output_urn,
                         bool truncate = true);

class BasicImager {
  protected:
    std::vector<URN> volume_URNs;

    // The current URN of the volume we write to.
    URN output_volume_urn;

    // The current URN of the backing file we are writing to.
    URN output_volume_backing_urn;

    // For multipart volume.
    int output_volume_part = 0;

    // Maximum size of output volume.
    size_t max_output_volume_file_size = 0;

    // Type of compression we should use.
    AFF4_IMAGE_COMPRESSION_ENUM compression = AFF4_IMAGE_COMPRESSION_ENUM_ZLIB;

    /**
     * When this is set the imager will try to abort as soon as possible.
     *
     * Any streams it is currently writing will be closed and flushed. Any volumes
     * will be finalized. The imager will then return from the Run() method with
     * an AFF4Status of ABORT.
     */
    bool should_abort = false;

    // As actions are executed they are added to this.
    std::set<std::string> actions_run;

    // The list of input streams we copy (from the --inputs flag).
    std::vector<std::string> inputs;

    virtual std::string GetName() {
        return "AFF4 Imager";
    }

    virtual std::string GetVersion() {
        return AFF4_VERSION;
    }

    AFF4Status GetOutputVolumeURN(URN& volume_urn);

    std::unique_ptr<TCLAP::CmdLine> cmd;

    virtual AFF4Status handle_logging();
    virtual AFF4Status handle_aff4_volumes();
    virtual AFF4Status handle_view();
    virtual AFF4Status handle_list();
    virtual AFF4Status parse_input();
    virtual AFF4Status process_input();
    virtual AFF4Status handle_export();
    virtual AFF4Status handle_compression();

    /**
     * This method should be called by imager programs to parse the command line
     * and set internal state. The method should not actually do anything other
     * than interpret and copy the command line. This method could be called
     * multiple times! If you extend this class, you must also call this method of
     * the base class - especially if you also need to check flags parsed by the
     * base class.
     *
     * @param argc
     * @param argv
     *
     * @return STATUS_OK if it is ok to continue processing. Otherwise the program
     * should not call the Run() method.
     */
    virtual AFF4Status ParseArgs();

    /**
     * This method is called after the args are parsed. This is where we actually
     * process the args and do things.
     *
     *
     * @return STATUS_OK if the method actually ran something, CONTINUE if there
     * was nothing to do.
     */
    virtual AFF4Status ProcessArgs();

    // We use a list here to preserve insertion order.
    std::list<std::unique_ptr<TCLAP::Arg>> args;

    void AddArg(TCLAP::Arg* arg) {
        args.push_back(std::unique_ptr<TCLAP::Arg>(arg));
    }

    TCLAP::Arg* Get(std::string name) {
        for (auto it = args.begin(); it != args.end(); it++) {
            if ((*it)->getName() == name) {
                return it->get();
            }
        }

        resolver.logger->critical("Parameter {} not known", name);
        abort();
    }

    template<typename T>
    T* GetArg(std::string name) {
        return dynamic_cast<T*>(Get(name));
    }

    /**
     * Expands the glob into a list of filenames that would match. This
     * functionality is especially required on windows where users have no shell
     * expansion.
     *
     * @param glob
     *
     * @return expanded filenames.
     */
    std::vector<std::string> GlobFilename(std::string glob) const;

  public:
    MemoryDataStore resolver;

    /**
     * This should be overloaded for imagers that need to do something before they
     * start. The method is called during the imager's initialization routine.
     *
     * By default we initialize signal handler.
     *
     * @return If this returns anything other that STATUS_OK we abort.
     */
    virtual AFF4Status Initialize();

    virtual AFF4Status RegisterArgs() {
        AddArg(new TCLAP::SwitchArg("V", "view", "View AFF4 metadata", false));
        AddArg(new TCLAP::SwitchArg("l", "list", "List all image streams in the volume.", false));
        AddArg(new TCLAP::MultiSwitchArg(
                   "d", "debug", "Display debugging logging (repeat for more info)",
                   false));

        AddArg(new TCLAP::SwitchArg(
                   "v", "verbose", "Display more verbose information", false));

        AddArg(new TCLAP::SwitchArg(
                   "t", "truncate", "Truncate the output file. Normally volumes and "
                   "images are appended to existing files, but this flag forces the "
                   "output file to be truncated first.",
                   false));

        AddArg(new TCLAP::MultiArgToNextFlag(
                   "i", "input", "File to image. If specified we copy these file to the "
                   "output volume located at --output. If there is no AFF4 volume on "
                   "--output yet, we create a new volume on it. An filename of @ means "
                   "to read filenames from stdin (In this case no glob expansion will "
                   "occur). \n"
                   "This can be specified multiple times with shell expansion. e.g.:\n"
                   "-i /bin/*",
                   false, "/path/to/file/or/device"));

        AddArg(new TCLAP::ValueArg<std::string>(
                   "e", "export", "Name of the streams to export. If specified we try "
                   "to open this stream and write it to the export directory. Note that "
                   "you will also need to specify an AFF4 volume path to load so we know "
                   "where to find the stream. Specifying a relative URN "
                   "implies a stream residing in a loaded volume. E.g.\n"

                   " -e /dev/sda -o /tmp/myfile my_volume.aff4",
                   false, "", "string"));

        AddArg(new TCLAP::ValueArg<std::string>(
                   "D", "export_dir", "Directory to export to (Defaults to current "
                   "directory)",
                   false, ".", "path to directory"));

        AddArg(new TCLAP::ValueArg<std::string>(
                   "o", "output", "Output file to write to. If the file does not "
                   "exist we create it. NOTE: If output is a directory we write an "
                   "AFF4 Directory volume when imaging.", false, "",
                   "/path/to/file"));

        AddArg(new TCLAP::SizeArg(
                   "s", "split", "Split output volumes at this size.", false, 0,
                   "Size (E.g. 100Mb)"));

        AddArg(new TCLAP::ValueArg<std::string>(
                   "c", "compression", "Type of compression to use (default zlib).",
                   false, "", "zlib, snappy, none"));

        AddArg(new TCLAP::ValueArg<int>(
                   "", "threads", "Total number of threads to use.",
                   false, 2, "(default 2)"));

        AddArg(new TCLAP::UnlabeledMultiArg<std::string>(
                   "aff4_volumes",
                   "These AFF4 Volumes will be loaded and their metadata will "
                   "be parsed before the program runs.\n"
                   "Note that this is necessary before you can extract streams with the "
                   "--export flag.",
                   false, "/path/to/aff4/volume"));

        return STATUS_OK;
    }

    virtual AFF4Status Run(int argc, char** argv);

    virtual void Abort();
    virtual ~BasicImager() {}

    // Should be called periodically to give the imager a chance to
    // swap output volume if the output needs to be split.
    void MaybeSwapOutputVolume(const URN &image_stream);

 private:
    AFF4Status GetNextPart();

};

} // namespace aff4

#endif  // SRC_AFF4_IMAGER_UTILS_H_
