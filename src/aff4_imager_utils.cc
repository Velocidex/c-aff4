/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/
#include "libaff4.h"
#include "aff4_imager_utils.h"
#include <iostream>
#include <time.h>

//using std::cout;

// This will be flipped by the signal handler.
AFF4Status ImageStream(DataStore& resolver, std::vector<URN>& input_urns,
                       URN output_urn,
                       bool truncate,
                       size_t buffer_size) {
    UNUSED(buffer_size);
    AFF4Status result = STATUS_OK;

    // We are allowed to write on the output file.
    if (truncate) {
        LOG(INFO) << "Truncating output file: " << output_urn.value.c_str();
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    } else {
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    }

    AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
                                           output_urn);

    if (!output) {
        LOG(ERROR) << "Failed to create output file: " << output_urn.value.c_str()
                   << ".\n";
        return IO_ERROR;
    }

    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, output->urn);
    if (!zip) {
        return IO_ERROR;
    }

    for (URN input_urn : input_urns) {
        std::cout << "Adding " << input_urn.value.c_str() << "\n";

        AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
                                              input_urn);

        if (!input) {
            LOG(ERROR) << "Failed to open input file: " << input_urn.value.c_str()
                       << ".\n";
            result = IO_ERROR;
            continue;
        }

        // Create a new image in this volume.
        URN image_urn = zip->urn.Append(input_urn.Parse().path);

        AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
                                             &resolver, image_urn, zip->urn);

        if (!image) {
            return IO_ERROR;
        }

        DefaultProgress progress;
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
                         bool truncate,
                         size_t buffer_size) {
    UNUSED(buffer_size);
    // We are allowed to write on the output file.
    if (truncate) {
        LOG(INFO) << "Truncating output file: " << output_urn.value.c_str();
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    } else {
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    }

    AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
                                          input_urn);
    AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
                                           output_urn);

    if (!input) {
        LOG(ERROR) << "Failed to open input stream. " <<
                   input_urn.SerializeToString() << "\n";
        return IO_ERROR;
    }

    if (!output) {
        LOG(ERROR) << "Failed to create output file: " <<
                   output_urn.SerializeToString() << "\n";
        return IO_ERROR;
    }

    DefaultProgress progress;
    progress.length = input->Size();

    AFF4Status res = output->WriteStream(input.get(), &progress);
    return res;
}


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
        LOG(ERROR) << e.error() << " " << e.argId();
        return GENERIC_ERROR;
    }

    if (res == CONTINUE) {
        res = ProcessArgs();
    }

    return res;
}

AFF4Status BasicImager::ParseArgs() {
    AFF4Status result = CONTINUE;

    // Check for incompatible commands.
    if (Get("export")->isSet() && Get("input")->isSet()) {
        std::cout << "--export and --input are incompatible. "
                  "Please select only one.\n";
        return INCOMPATIBLE_TYPES;
    }

    if (result == CONTINUE && Get("input")->isSet()) {
        result = parse_input();
    }

    if (result == CONTINUE && Get("compression")->isSet()) {
        result = handle_compression();
    }

    if (result == CONTINUE && Get("debug")->isSet()) {
        result = handle_Debug();
    }

    if (result == CONTINUE && Get("aff4_volumes")->isSet()) {
        result = handle_aff4_volumes();
    }

    return result;
}

AFF4Status BasicImager::ProcessArgs() {
    AFF4Status result = CONTINUE;

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


AFF4Status BasicImager::handle_Debug() {
    google::SetStderrLogging(google::GLOG_INFO);

    return CONTINUE;
}

AFF4Status BasicImager::handle_aff4_volumes() {
    std::vector<std::string> v = GetArg<TCLAP::UnlabeledMultiArg<std::string>>(
                                     "aff4_volumes")->getValue();

    for (unsigned int i = 0; i < v.size(); i++) {
        URN urn = URN::NewURNFromFilename(v[i]);
        LOG(INFO) << "Preloading AFF4 Volume: " << urn.SerializeToString();

        // Currently we support AFF4Directory and ZipFile:
        if (AFF4Directory::IsDirectory(urn)) {
            AFF4ScopedPtr<AFF4Directory> volume = AFF4Directory::NewAFF4Directory(
                    &resolver, urn);

            if (!volume) {
                LOG(ERROR) << "Directory " << v[i] << " does not appear to be "
                           "a valid AFF4 volume.";
                return IO_ERROR;
            }

            volume_URN = volume->urn;
        } else {
            AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, urn);
            if (zip.get() == nullptr || zip->members.size() == 0) {
                LOG(ERROR) << "Unable to load " << v[i]
                           << " as an existing AFF4 Volume.";
                return IO_ERROR;
            }

            volume_URN = zip->urn;
        }
    }

    return CONTINUE;
}

AFF4Status BasicImager::handle_view() {
    resolver.Dump(GetArg<TCLAP::SwitchArg>("verbose")->getValue());

    // After running the View command we are done.
    return STATUS_OK;
}

AFF4Status BasicImager::parse_input() {
    inputs = GetArg<TCLAP::MultiArgToNextFlag<std::string>>(
                 "input")->getValue();

    return CONTINUE;
}

AFF4Status BasicImager::process_input() {
    // Get the output volume.
    URN volume_urn;
    AFF4Status res = GetOutputVolumeURN(volume_urn);
    if (res != STATUS_OK) {
        return res;
    }

    AFF4ScopedPtr<AFF4Volume> volume = resolver.AFF4FactoryOpen<AFF4Volume>(
                                           volume_urn);

    for (std::string glob : inputs) {
        for (std::string input : GlobFilename(glob)) {
            URN input_urn(URN::NewURNFromFilename(input, false));

            std::cout << "Adding " << input.c_str() << " as " <<
                      input_urn.SerializeToString() << "\n";

            // Try to open the input.
            AFF4ScopedPtr<AFF4Stream> input_stream = resolver.AFF4FactoryOpen<
                    AFF4Stream>(input_urn);

            // Not valid - skip it.
            if (!input_stream) {
                LOG(ERROR) << "Unable to find " << input_urn.SerializeToString();
                res = CONTINUE;
                continue;
            }

            // Create a new AFF4Image in this volume.
            URN image_urn = volume_urn.Append(input_urn.Parse().path);

            // Store the original filename.
            resolver.Set(image_urn, AFF4_STREAM_ORIGINAL_FILENAME,
                         new XSDString(input));

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

                // Copy the input stream to the output stream.
                res = image_stream->WriteStream(input_stream.get(), &empty_progress);
                if (res != STATUS_OK) {
                    return res;
                }

                // We need to explicitly check the abort status here.
                if (should_abort || aff4_abort_signaled) {
                    return ABORTED;
                }

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

                DefaultProgress progress;
                progress.length = input_stream->Size();

                // Copy the input stream to the output stream.
                res = image_stream->WriteStream(input_stream.get(), &progress);
                if (res != STATUS_OK) {
                    return res;
                }
            }
        }
    }

    actions_run.insert("input");
    return CONTINUE;
}

AFF4Status BasicImager::handle_export() {
    if (!Get("output")->isSet()) {
        std::cout << "ERROR: Can not specify an export without an output\n";
        return INVALID_INPUT;
    }

    std::string output = GetArg<TCLAP::ValueArg<std::string>>("output")->getValue();
    std::string export_ = GetArg<TCLAP::ValueArg<std::string>>("export")->getValue();
    URN export_urn(export_);
    URN output_urn(output);

    // We do not want to interpret this parameter as a file reference since it
    // must come from the image.
    if (volume_URN.value.size() > 0 &&
            export_urn.Scheme() == "file") {
        LOG(INFO) << "Interpreting export URN as relative to volume " <<
                  volume_URN.value;

        export_urn = volume_URN.Append(export_);
    }

    // When we export we always truncate the output file.
    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE,
                 new XSDString("truncate"));

    // Hold the volume in use while we extract the stream for efficiency.
    AFF4ScopedPtr <AFF4Volume> volume = resolver.AFF4FactoryOpen<AFF4Volume>(
                                            volume_URN);

    if (!volume) {
        LOG(ERROR) << "Unable to open volume " << volume_URN.SerializeToString();
        return IO_ERROR;
    }

    std::cout << "Extracting " << export_urn.value << " into " <<
              output_urn.value << "\n";
    AFF4Status res = ExtractStream(
                         resolver, export_urn, output_urn, Get("truncate")->isSet());

    if (res == STATUS_OK) {
        actions_run.insert("export");
        return CONTINUE;
    }

    return res;
}


AFF4Status BasicImager::GetOutputVolumeURN(URN& volume_urn) {
    if (output_volume_urn.value.size() > 0) {
        volume_urn = output_volume_urn;
        return STATUS_OK;
    }

    if (!Get("output")->isSet()) {
        return INVALID_INPUT;
    }

    std::string output_path = GetArg<TCLAP::ValueArg<std::string>>("output")->getValue();
    URN output_urn(URN::NewURNFromFilename(output_path));

    // We are allowed to write on the output file.
    if (Get("truncate")->isSet()) {
        std::cout << "Output file " << output_urn.SerializeToString() <<
                  " will be truncated.\n";;

        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    } else {
        resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    }

    // The output is a directory volume.
    if (AFF4Directory::IsDirectory(output_path)) {
        AFF4ScopedPtr<AFF4Directory> volume = AFF4Directory::NewAFF4Directory(
                &resolver, output_urn);

        if (!volume) {
            return IO_ERROR;
        }

        volume_urn = volume->urn;
        output_volume_urn = volume->urn;

        std::cout << "Creating output AFF4 Directory structure.\n";
        return STATUS_OK;
    }

    // The output is a ZipFile volume.
    AFF4ScopedPtr<AFF4Stream> output_stream = resolver.AFF4FactoryOpen
            <AFF4Stream>(output_urn);

    if (!output_stream) {
        LOG(ERROR) << "Failed to create output file: " <<
                   output_urn.SerializeToString() << ":" << GetLastErrorMessage();

        return IO_ERROR;
    }

    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
                                     &resolver, output_stream->urn);

    if (!zip) {
        return IO_ERROR;
    }

    volume_urn = zip->urn;
    output_volume_urn = zip->urn;

    std::cout << "Creating output AFF4 ZipFile.\n";

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
        LOG(ERROR) << "Unknown compression scheme " << compression;
        return INVALID_INPUT;
    }

    std::cout << "Setting compression " << compression_setting.c_str() << "\n";

    return CONTINUE;
}


#ifdef _WIN32
// We only allow a wild card in the last component.
vector<string> BasicImager::GlobFilename(string glob) const {
    vector<string> result;
    WIN32_FIND_DATA ffd;
    unsigned int found = glob.find_last_of("/\\");
    string path = "";

    // The path before the last PATH_SEP
    if (found != string::npos) {
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

    for (int i = 0; i < glob_data.gl_pathc; i++) {
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
        LOG(ERROR) << "Unable to set interrupt handler: " <<
                   GetLastErrorMessage();
    }
#else
    signal(SIGINT, sigint_handler);
#endif

    return STATUS_OK;
}
