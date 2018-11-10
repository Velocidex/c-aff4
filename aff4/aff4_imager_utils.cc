/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/

#include "aff4/libaff4.h"
#include "aff4/aff4_imager_utils.h"
#include "aff4/rdf.h"
#include <iostream>
#include <string>
#include <time.h>
#include <memory>
#include "spdlog/spdlog.h"

namespace aff4 {

// High level functions for imaging and extracting streams. Can be
// used without an imager instance.
AFF4Status ImageStream(DataStore& resolver, const std::vector<URN>& input_urns,
                       URN output_urn,
                       bool truncate) {
    AFF4Status result = STATUS_OK;

    // We are allowed to write on the output file.
    if (truncate) {
        resolver.logger->info("Truncating output file: {}", output_urn.value);
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    } else {
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    }

    AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
                                           output_urn);

    if (!output) {
        resolver.logger->error("Failed to create output file: {}.", output_urn.value);
        return IO_ERROR;
    }

    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, output->urn);
    if (!zip) {
        return IO_ERROR;
    }

    for (URN input_urn : input_urns) {
        resolver.logger->info("Adding {}", input_urn);

        AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
                                              input_urn);

        if (!input) {
            resolver.logger->error("Failed to open input file: {}", input_urn.value);
            result = IO_ERROR;
            continue;
        }

        // Create a new image in this volume.
        URN image_urn = zip->urn.Append(input_urn.Path());

        AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
                                             &resolver, image_urn, zip->urn);

        if (!image) {
            return IO_ERROR;
        }

        DefaultProgress progress(&resolver);
        progress.length = input->Size();

        AFF4Status res = image->WriteStream(input.get(), &progress);
        if (res != STATUS_OK) {
            return res;
        }
    }

    return result;
}


AFF4Status ExtractStream(DataStore& resolver, URN input_urn,
                         URN output_urn,
                         bool truncate) {
    // We are allowed to write on the output file.
    if (truncate) {
        resolver.logger->info("Truncating output file: {}", output_urn.value);
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    } else {
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    }

    AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
                                          input_urn);
    AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
                                           output_urn);

    if (!input) {
        resolver.logger->error("Failed to open input stream {}",
                               input_urn.SerializeToString());
        return IO_ERROR;
    }

    if (!output) {
        resolver.logger->error("Failed to create output file: {}",
                               output_urn.SerializeToString());
        return IO_ERROR;
    }

    DefaultProgress progress(&resolver);
    progress.length = input->Size();

    AFF4Status res = output->WriteStream(input.get(), &progress);
    return res;
}


// A Progress indicator which keeps tabs on the size of the output volume.
// This is used to support output volume splitting.
class VolumeManager : public DefaultProgress {
public:
    VolumeManager(DataStore *resolver, BasicImager *imager,
                  const URN& image_urn) :
        DefaultProgress(resolver), imager(imager), image_urn(image_urn) {}

    bool Report(aff4_off_t readptr) override {
        imager->MaybeSwapOutputVolume(image_urn);

        return DefaultProgress::Report(readptr);
    }

protected:
    // Not owned.
    BasicImager *imager;

    URN image_urn;
};


AFF4Status BasicImager::Run(int argc, char** argv)  {
    AFF4Status res = Initialize();
    if (res != STATUS_OK) {
        return res;
    }

    RegisterArgs();

    TCLAP::CmdLine cmd(GetName(), ' ', GetVersion());

    for (auto it = args.rbegin(); it != args.rend(); it++) {
        cmd.add(it->get());
    }

    try {
        cmd.parse(argc, argv);
        res =  ParseArgs();
    } catch(const TCLAP::ArgException& e) {
        resolver.logger->error("Error {} {}", e.error(), e.argId());
        return GENERIC_ERROR;
    }

    if (res == CONTINUE) {
        res = ProcessArgs();
    }

    return res;
}

AFF4Status BasicImager::ParseArgs() {
    AFF4Status result = handle_logging();

    if (Get("threads")->isSet()) {
        int threads = GetArg<TCLAP::ValueArg<int>>("threads")->getValue();
        resolver.logger->info("Will use {} threads.", threads);
        resolver.pool.reset(new ThreadPool(threads));
    }

    // Check for incompatible commands.
    if (Get("export")->isSet() && Get("input")->isSet()) {
        resolver.logger->critical(
            "The --export and --input flags are incompatible. "
            "Please select only one.");
        return INCOMPATIBLE_TYPES;
    }

    if (result == CONTINUE) {
        result = parse_input();
    }

    if (result == CONTINUE && Get("compression")->isSet()) {
        result = handle_compression();
    }

    if (result == CONTINUE && Get("aff4_volumes")->isSet()) {
        result = handle_aff4_volumes();
    }

    if (result == CONTINUE && Get("split")->isSet()) {
        max_output_volume_file_size = GetArg<TCLAP::SizeArg>(
            "split")->getValue();
        resolver.logger->info("Output volume will be limited to {} bytes",
                              max_output_volume_file_size);
    }

    return result;
}

AFF4Status BasicImager::ProcessArgs() {
    AFF4Status result = CONTINUE;

    if (Get("list")->isSet()) {
        result = handle_list();
    }

    if (result == CONTINUE && Get("view")->isSet()) {
        result = handle_view();
    }

    if (result == CONTINUE && Get("export")->isSet()) {
        result = handle_export();
    }

    if (result == CONTINUE && inputs.size() > 0) {
        result = process_input();
    }

    return result;
}


AFF4Status BasicImager::handle_logging() {
    if (Get("logfile")->isSet()) {
        std::vector<spdlog::sink_ptr> sinks = resolver.logger->sinks();

        auto new_sink = std::make_shared<spdlog::sinks::simple_file_sink_mt>(
            GetArg<TCLAP::ValueArg<std::string>>(
                "logfile")->getValue());
        sinks.push_back(new_sink);

        resolver.logger = std::make_shared<spdlog::logger>(
                     "", sinks.begin(), sinks.end());
    }

    int level = GetArg<TCLAP::MultiSwitchArg>("debug")->getValue();
    switch(level) {
    case 0:
        resolver.logger->set_level(spdlog::level::err);
        break;

    case 1:
        resolver.logger->set_level(spdlog::level::warn);
        break;

    case 2:
        resolver.logger->set_level(spdlog::level::info);
        break;

    case 3:
        resolver.logger->set_level(spdlog::level::debug);
        break;

    default:
        resolver.logger->set_level(spdlog::level::trace);
        break;
    }


    resolver.logger->set_pattern("%Y-%m-%d %T %L %v");

    return CONTINUE;
}

AFF4Status BasicImager::handle_aff4_volumes() {
    auto volumes = GetArg<TCLAP::UnlabeledMultiArg<std::string>>(
        "aff4_volumes")->getValue();

    for (const auto &volume_to_load: volumes) {
        URN urn = URN::NewURNFromFilename(volume_to_load);
        resolver.logger->info("Preloading AFF4 Volume: {}", urn.SerializeToString());

        // Currently we support AFF4Directory and ZipFile. If the
        // directory does not already exist, and the argument ends
        // with a / then we create a new directory.
        if (AFF4Directory::IsDirectory(urn, /* must_exist= */ false)) {
            AFF4ScopedPtr<AFF4Directory> volume = AFF4Directory::NewAFF4Directory(
                    &resolver, urn);

            if (!volume) {
                resolver.logger->error("Directory {}  does not appear to be "
                                       "a valid AFF4 volume.", volume_to_load);
                return IO_ERROR;
            }

            volume_URNs.push_back(volume->urn);
        } else {
            AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, urn);
            if (zip.get() == nullptr || zip->members.size() == 0) {
                resolver.logger->error(
                    "Unable to load {} as an existing AFF4 Volume (skipping).",
                    volume_to_load);
                continue;
            }

            volume_URNs.push_back(zip->urn);
        }
    }

    return CONTINUE;
}

AFF4Status BasicImager::handle_list() {
    URN image_type(AFF4_IMAGE_TYPE);
    for (const auto& subject: resolver.Query(
             URN(AFF4_TYPE), &image_type)) {
        std::cout << subject.SerializeToString() << "\n";
    }

    return STATUS_OK;
}


AFF4Status BasicImager::handle_view() {
    resolver.Dump(GetArg<TCLAP::SwitchArg>("verbose")->getValue());

    // After running the View command we are done.
    return STATUS_OK;
}

AFF4Status BasicImager::parse_input() {
    for (const auto& input: GetArg<TCLAP::MultiArgToNextFlag>("input")->getValue()) {
        // Read input files from stdin - this makes it easier to support
        // files with spaces and special shell chars in their names.
        if (input == "@") {
            for(std::string line;;) {
                std::getline (std::cin, line);
                if (line.size() == 0) break;
                inputs.push_back(line);
            }
        } else {
            inputs.push_back(input);
        }
    }

    return CONTINUE;
}

AFF4Status BasicImager::process_input() {
    // Get the output volume.
    for (std::string glob : inputs) {
        for (std::string input : GlobFilename(glob)) {
            resolver.logger->debug("Will add file {}", input);
            URN input_urn(URN::NewURNFromFilename(input, false));
            resolver.logger->info("Adding {} as {}", input, input_urn);

            // Try to open the input.
            AFF4ScopedPtr<AFF4Stream> input_stream = resolver.AFF4FactoryOpen<
                    AFF4Stream>(input_urn);

            // Not valid - skip it.
            if (!input_stream) {
                resolver.logger->error("Unable to find {}", input_urn);
                continue;
            }

            URN volume_urn;
            RETURN_IF_ERROR(GetOutputVolumeURN(&volume_urn));

            AFF4ScopedPtr<AFF4Volume> volume = resolver.AFF4FactoryOpen<
                AFF4Volume>(volume_urn);

            URN image_urn;
            // Create a new AFF4Image in this volume.
            if (GetArg<TCLAP::SwitchArg>("relative")->getValue()) {
                char cwd[PATH_MAX];
                if (getcwd(cwd, PATH_MAX) == NULL) {
                    return IO_ERROR;
                }

                URN current_dir_urn = URN::NewURNFromFilename(cwd, false);
                image_urn.Set(volume_urn.Append(current_dir_urn.RelativePath(input_urn)));
            } else {
                image_urn.Set(volume_urn.Append(input_urn.Path()));

                // Store the original filename.
                resolver.Set(image_urn, AFF4_STREAM_ORIGINAL_FILENAME,
                             new XSDString(input));
            }
            // For very small streams, it is more efficient to just store them without
            // compression. Also if the user did not ask for compression, there is no
            // advantage in storing a Bevy based image, just store it in one piece.
            if (compression == AFF4_IMAGE_COMPRESSION_ENUM_STORED ||
                    input_stream->Size() < 10 * 1024 * 1024) {
                AFF4ScopedPtr<AFF4Stream> image_stream = volume->CreateMember(
                            image_urn);

                if (!image_stream) {
                    return IO_ERROR;
                }

                // If the underlying stream supports compression, lets do that.
                image_stream->compression_method = compression;

                // We only support deflate compression on zip segments.
                if (compression != AFF4_IMAGE_COMPRESSION_ENUM_STORED) {
                    image_stream->compression_method = AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE;
                }

                // Copy the input stream to the output stream.
                ProgressContext progress(&resolver);
                RETURN_IF_ERROR(image_stream->WriteStream(input_stream.get(), &progress));

                // Make this stream as an Image (Should we have
                // another type for a LogicalImage?
                resolver.Set(image_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE),
                             /* replace = */ false);

                // We need to explicitly check the abort status here.
                if (should_abort || aff4_abort_signaled) {
                    return ABORTED;
                }

                resolver.Close(input_stream);
                resolver.Close(image_stream);

                // Otherwise use an AFF4Image.
            } else {
                AFF4ScopedPtr<AFF4Image> image_stream = AFF4Image::NewAFF4Image(
                        &resolver, image_urn, volume_urn);

                // Cant write to the output stream at all, this is considered fatal.
                if (!image_stream) {
                    return IO_ERROR;
                }

                // Set the output compression according to the user's wishes.
                image_stream->compression = compression;

                VolumeManager progress(&resolver, this, image_urn);
                progress.length = input_stream->Size();

                // Copy the input stream to the output stream.
                RETURN_IF_ERROR(image_stream->WriteStream(input_stream.get(), &progress));

                // Make this stream as an Image (Should we have
                // another type for a LogicalImage?
                resolver.Set(image_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE),
                             /* replace = */ false);

                resolver.Close(input_stream);
                resolver.Close(image_stream);
            }
        }
    }

    actions_run.insert("input");

    return CONTINUE;
}

AFF4Status BasicImager::handle_export() {
    if (Get("output")->isSet()) {
        resolver.logger->error(
            "Can not specify an export and an output volume at the same time "
            "(did you mean --export_dir).");
        return INVALID_INPUT;
    }

    std::string export_dir = GetArg<TCLAP::ValueArg<std::string>>(
        "export_dir")->getValue();
    URN export_dir_urn = URN::NewURNFromFilename(export_dir, true);
    std::vector<URN> urns;
    std::string export_pattern = GetArg<TCLAP::ValueArg<std::string>>(
        "export")->getValue();

    // A pattern of @ means to read all subjects from stdin.
    if (export_pattern == "@") {
        for(std::string line;;) {
            std::getline (std::cin, line);
            if (line.size() == 0) break;
            urns.push_back(line);
            resolver.logger->info("Found image {}", line);
        }
    } else {
        // These are all acceptable stream types.
        for (const URN image_type : std::vector<URN>{
                URN(AFF4_IMAGE_TYPE),
                    URN(AFF4_MAP_TYPE),
                    }) {
            for (const URN& image: resolver.Query(AFF4_TYPE, &image_type)) {
                if (aff4::fnmatch(
                        export_pattern.c_str(),
                        image.SerializeToString().c_str()) == 0) {
                    resolver.logger->info("Found image {}", image);
                    urns.push_back(image);
                }
            }
        }
    }

    for (const URN& export_urn: urns) {
        // Prepend the domain (AFF4 volume) to the export directory to
        // make sure the exported stream is unique.
        URN output_urn = export_dir_urn.Append(
            export_urn.Domain()).Append(
                export_urn.Path());
        resolver.logger->info("Writing to {}", output_urn);

        // Hold all the volumes in use while we extract the streams
        // for efficiency. This prevents volumes from being opened and
        // closed on demand under high load.
        std::vector<AFF4ScopedPtr<AFF4Volume>> volumes;
        for (const auto& volume_urn: volume_URNs) {
            auto volume = resolver.AFF4FactoryOpen<AFF4Volume>(volume_urn);
            if (!volume) {
                resolver.logger->error("Unable to open volume {}", volume_urn);
                return IO_ERROR;
            }

            volumes.push_back(std::move(volume));
        }

        resolver.logger->info("Extracting {} into {}", export_urn, output_urn);
        RETURN_IF_ERROR(
            ExtractStream(
                resolver, export_urn, output_urn, /* truncate = */ true));
    }

    actions_run.insert("export");
    return CONTINUE;
}


AFF4Status BasicImager::GetNextPart() {
        output_volume_part ++;
        output_volume_urn = "";
        return GetOutputVolumeURN(&output_volume_urn);
}


AFF4Status BasicImager::GetOutputVolumeURN(URN* volume_urn) {
    if (output_volume_urn.value.size() > 0) {
        *volume_urn = output_volume_urn;
        return STATUS_OK;
    }

    if (!Get("output")->isSet()) {
        return INVALID_INPUT;
    }

    std::string output_path = GetArg<TCLAP::ValueArg<std::string>>(
        "output")->getValue();

    if (output_path == "-") {
        output_volume_backing_urn = URN("builtin://stdout");
    } else {
        if (output_volume_part > 0) {
            output_path = aff4_sprintf("%s.A%02d", output_path.c_str(),
                                       output_volume_part);
        }

        output_volume_backing_urn = URN::NewURNFromFilename(output_path);
    }

    // We are allowed to write on the output file.
    if (Get("truncate")->isSet()) {
        resolver.logger->warn("Output file {} will be truncated.",
                              output_volume_backing_urn);
        resolver.Set(output_volume_backing_urn,
                     AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    } else {
        resolver.Set(output_volume_backing_urn,
                     AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    }

    // The output is a directory volume.
    if (AFF4Directory::IsDirectory(output_path, /* must_exist= */ true)) {
        AFF4ScopedPtr<AFF4Directory> volume = AFF4Directory::NewAFF4Directory(
                &resolver, output_volume_backing_urn);

        if (!volume) {
            return IO_ERROR;
        }

        *volume_urn = volume->urn;
        output_volume_urn = volume->urn;

        resolver.logger->info("Creating output AFF4 Directory structure.");
        return STATUS_OK;
    }

    // The output is a ZipFile volume.
    AFF4ScopedPtr<AFF4Stream> output_stream = resolver.AFF4FactoryOpen
            <AFF4Stream>(output_volume_backing_urn);

    if (!output_stream) {
        resolver.logger->error("Failed to create output file: {}: {}",
                               output_volume_backing_urn,
                               GetLastErrorMessage());

        return IO_ERROR;
    }

    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
        &resolver, output_stream->urn);

    if (!zip) {
        return IO_ERROR;
    }

    *volume_urn = zip->urn;
    output_volume_urn = zip->urn;

    resolver.logger->info("Creating output AFF4 ZipFile.");

    return STATUS_OK;
}


AFF4Status BasicImager::handle_compression() {
    std::string compression_setting = GetArg<TCLAP::ValueArg<std::string>>(
                                          "compression")->getValue();

    if (compression_setting == "zlib") {
        compression = AFF4_IMAGE_COMPRESSION_ENUM_ZLIB;
    } else if (compression_setting == "snappy") {
        compression = AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY;

    } else if (compression_setting == "none") {
        compression = AFF4_IMAGE_COMPRESSION_ENUM_STORED;

    } else {
        resolver.logger->error("Unknown compression scheme {}", compression);
        return INVALID_INPUT;
    }

    resolver.logger->info("Setting compression {}", compression_setting);

    return CONTINUE;
}

void BasicImager::MaybeSwapOutputVolume(const URN& image_stream) {
    size_t backing_file_size = 0;
    {
        AFF4ScopedPtr<AFF4Stream> output_stream = resolver.AFF4FactoryOpen<
            AFF4Stream>(output_volume_backing_urn);
        if (output_stream.get()) {
            backing_file_size = output_stream->Size();
        }
    }
    // We need to split the volume into another file.
    if (max_output_volume_file_size > 0 &&
        backing_file_size > max_output_volume_file_size) {
        {
            AFF4ScopedPtr<AFF4Volume> volume = resolver.AFF4FactoryOpen<
                AFF4Volume>(output_volume_urn);
            volume->Flush();
        }
        resolver.logger->warn("Volume {} is too large, Splitting into next volume.",
                              output_volume_backing_urn);
        // Make a new volume.
        GetNextPart();
        resolver.Set(image_stream, AFF4_STORED, new URN(output_volume_urn));
    }
}


#ifdef _WIN32
// We only allow a wild card in the last component.
std::vector<std::string> BasicImager::GlobFilename(std::string glob) const {
    std::vector<std::string> result;
    WIN32_FIND_DATA ffd;
    unsigned int found = glob.find_last_of("/\\");
    std::string path = "";

    // The path before the last PATH_SEP
    if (found != std::string::npos) {
        path = glob.substr(0, found);
    }

    HANDLE hFind = FindFirstFile(glob.c_str(), &ffd);
    if (INVALID_HANDLE_VALUE != hFind) {
        do {
            // If it is not a directory, add a result.
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (path.size() > 0) {
                    result.push_back(path + PATH_SEP_STR + ffd.cFileName);
                } else {
                    result.push_back(ffd.cFileName);
                }
            }
        } while (FindNextFile(hFind, &ffd) != 0);
    }
    FindClose(hFind);

    return result;
}
#else
#include <glob.h>

std::vector<std::string> BasicImager::GlobFilename(std::string glob_expression) const {
    std::vector<std::string> result;
    glob_t glob_data;

    int res = glob(glob_expression.c_str(),
                   GLOB_MARK|GLOB_BRACE|GLOB_TILDE,
                   nullptr,  // errfunc
                   &glob_data);

    if (res == GLOB_NOSPACE) {
        return result;
    }

    for (unsigned int i = 0; i < glob_data.gl_pathc; i++) {
        result.push_back(glob_data.gl_pathv[i]);
    }

    globfree(&glob_data);

    return result;
}
#endif

void BasicImager::Abort() {
    // Tell everything to wind down.
    should_abort = true;
}

#ifdef _WIN32
BOOL sigint_handler(DWORD dwCtrlType) {
    UNUSED(dwCtrlType);
    aff4_abort_signaled = true;

    return TRUE;
}
#else
#include <signal.h>
void sigint_handler(int s) {
    UNUSED(s);
    aff4_abort_signaled = true;
}
#endif

AFF4Status BasicImager::Initialize() {
#ifdef _WIN32
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)sigint_handler, true)) {
        resolver.logger->error("Unable to set interrupt handler: {}",
                               GetLastErrorMessage());
    }
#else
    signal(SIGINT, sigint_handler);
#endif

    return STATUS_OK;
}

} // namespace aff4
