/*
  Utilities for AFF4 imaging. These are mostly high level utilities used by the
  command line imager.
*/
#include "libaff4.h"
#include "aff4_imager_utils.h"
#include <iostream>
#include <time.h>

using std::cout;

AFF4Status ImageStream(DataStore &resolver, vector<URN> &input_urns,
                       URN output_urn,
                       bool truncate,
                       size_t buffer_size) {
  AFF4Status result = STATUS_OK;

  // We are allowed to write on the output file.
  if (truncate) {
    LOG(INFO) << "Truncating output file: " << output_urn.value.c_str();
    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
  } else {
    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
  };

  AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
      output_urn);

  if (!output) {
    LOG(ERROR) << "Failed to create output file: " << output_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, output->urn);
  if (!zip) {
    return IO_ERROR;
  };

  for (URN input_urn : input_urns) {
    cout << "Adding " << input_urn.value.c_str() << "\n";

    AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
        input_urn);

    if (!input) {
      LOG(ERROR) << "Failed to open input file: " << input_urn.value.c_str()
                 << ".\n";
      result = IO_ERROR;
      continue;
    };

    // Create a new image in this volume.
    URN image_urn = zip->urn.Append(input_urn.Parse().path);

    AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
        &resolver, image_urn, zip->urn);

    if (!image) {
      return IO_ERROR;
    };

    AFF4Status res = input->CopyToStream(*image, input->Size());
    if (res != STATUS_OK)
      return res;
  };

  return result;
};


AFF4Status ExtractStream(DataStore &resolver, URN input_urn,
                         URN output_urn,
                         bool truncate,
                         size_t buffer_size) {
  // We are allowed to write on the output file.
  if (truncate) {
    LOG(INFO) << "Truncating output file: " << output_urn.value.c_str();
    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
  } else {
    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
  };

  AFF4ScopedPtr<AFF4Stream> input = resolver.AFF4FactoryOpen<AFF4Stream>(
      input_urn);
  AFF4ScopedPtr<AFF4Stream> output = resolver.AFF4FactoryOpen<AFF4Stream>(
      output_urn);

  if (!input) {
    LOG(ERROR) << "Failed to open input stream. " << input_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  if (!output) {
    LOG(ERROR) << "Failed to create output file: " << output_urn.value.c_str()
               << ".\n";
    return IO_ERROR;
  };

  AFF4Status res = input->CopyToStream(*output, input->Size());
  return res;
};


AFF4Status BasicImager::Run(int argc, char** argv)  {
  AFF4Status res = Initialize();
  if (res != STATUS_OK)
    return res;

  RegisterArgs();

  TCLAP::CmdLine cmd(GetName(), ' ', GetVersion());

  for (auto it = args.rbegin(); it != args.rend(); it++) {
    cmd.add(it->get());
  };

  try {
    cmd.parse(argc, argv);
    res =  ParseArgs();
  } catch(const TCLAP::ArgException& e) {
    LOG(ERROR) << e.error() << " " << e.argId();
    return GENERIC_ERROR;
  }

  if (res == CONTINUE)
    res = ProcessArgs();

  return res;
};

AFF4Status BasicImager::ParseArgs() {
  AFF4Status result = CONTINUE;

  // Check for incompatible commands.
  if (Get("export")->isSet() && Get("input")->isSet()) {
    std::cout << "--export and --input are incompatible. "
        "Please select only one.\n";
    return INCOMPATIBLE_TYPES;
  };

  if (result == CONTINUE && Get("input")->isSet())
    result = parse_input();

  if (result == CONTINUE && Get("compression")->isSet())
    result = handle_compression();

  if (result == CONTINUE && Get("verbose")->isSet())
    result = handle_Verbose();

  if (result == CONTINUE && Get("aff4_volumes")->isSet())
    result = handle_aff4_volumes();

  return result;
};

AFF4Status BasicImager::ProcessArgs() {
  AFF4Status result = CONTINUE;

  if (result == CONTINUE && Get("view")->isSet())
    result = handle_view();

  if (result == CONTINUE && Get("export")->isSet())
    result = handle_export();

  if (result == CONTINUE && inputs.size() > 0)
    result = process_input();

  return result;
};


AFF4Status BasicImager::handle_Verbose() {
  google::SetStderrLogging(google::GLOG_INFO);

  return CONTINUE;
};

AFF4Status BasicImager::handle_aff4_volumes() {
  vector<string> v = GetArg<TCLAP::UnlabeledMultiArg<string>>(
      "aff4_volumes")->getValue();

  for (unsigned int i = 0; i < v.size(); i++) {
    LOG(INFO) << "Preloading AFF4 Volume: " << v[i];
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, v[i]);
    if (zip->members.size() == 0) {
      LOG(ERROR) << "Unable to load " << v[i]
                 << " as an existing AFF4 Volume.";
      return IO_ERROR;
    };

    volume_URN = zip->urn;
  };

  return CONTINUE;
};

AFF4Status BasicImager::handle_view() {
  resolver.Dump(GetArg<TCLAP::SwitchArg>("verbose")->getValue());

  // After running the View command we are done.
  return STATUS_OK;
}

AFF4Status BasicImager::parse_input() {
  inputs = GetArg<TCLAP::MultiArgToNextFlag<string>>(
      "input")->getValue();

  return CONTINUE;
};

AFF4Status BasicImager::process_input() {
  // Get the output volume.
  URN volume_urn;
  AFF4Status res = GetOutputVolumeURN(volume_urn);
  if (res != STATUS_OK)
    return res;

  for (string glob : inputs) {
    for (string input : GlobFilename(glob)) {
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
      };

      // Create a new AFF4Image in this volume.
      URN image_urn = volume_urn.Append(input_urn.Parse().path);

      // For very small streams, it is more efficient to just store them in a
      // ZipFileSegment.
      if (input_stream->Size() < 10 * 1024 * 1024) {
        AFF4ScopedPtr<ZipFileSegment> image_stream = ZipFileSegment::
            NewZipFileSegment(&resolver, image_urn, volume_urn);

        if (!image_stream)
          return IO_ERROR;

        image_stream->compression_method = ZIP_DEFLATE;
        // Copy the input stream to the output stream.
        res = input_stream->CopyToStream(
            *image_stream, input_stream->Size(), empty_progress);

        if (res != STATUS_OK)
          return res;

        // We need to explicitly check the abort status here.
        if (should_abort)
          return ABORTED;

        // Otherwise use an AFF4Image.
      } else {
        AFF4ScopedPtr<AFF4Image> image_stream = AFF4Image::NewAFF4Image(
            &resolver, image_urn, volume_urn);

        // Cant write to the output stream at all, this is considered fatal.
        if (!image_stream) {
          return IO_ERROR;
        };

        // Set the output compression according to the user's wishes.
        image_stream->compression = compression;

        // Copy the input stream to the output stream.
        res = input_stream->CopyToStream(
            *image_stream, input_stream->Size(),
            std::bind(&BasicImager::progress_renderer, this,
                      std::placeholders::_1, std::placeholders::_2));
        if (res != STATUS_OK)
          return res;
      };
    };
  };

  actions_run.insert("input");
  return CONTINUE;
}

AFF4Status BasicImager::handle_export() {
  if (!Get("output")->isSet()) {
    cout << "ERROR: Can not specify an export without an output\n";
    return INVALID_INPUT;
  };

  string output = GetArg<TCLAP::ValueArg<string>>("output")->getValue();
  string export_ = GetArg<TCLAP::ValueArg<string>>("export")->getValue();
  URN export_urn(export_);
  URN output_urn(output);

  // We do not want to interpret this parameter as a file reference since it
  // must come from the image.
  if (volume_URN.value.size() > 0 &&
      export_urn.Scheme() == "file") {
    LOG(INFO) << "Interpreting export URN as relative to volume " <<
        volume_URN.value;

    export_urn = volume_URN.Append(export_);
  };

  // When we export we always truncate the output file.
  resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE,
               new XSDString("truncate"));

  cout << "Extracting " << export_urn.value << " into " <<
      output_urn.value << "\n";
  AFF4Status res = ExtractStream(
      resolver, export_urn, output_urn, Get("truncate")->isSet());
  if (res == STATUS_OK)
    return CONTINUE;

  actions_run.insert("export");
  return res;
};


AFF4Status BasicImager::GetOutputVolumeURN(URN &volume_urn) {
  if (output_volume_urn.value.size() > 0) {
    volume_urn = output_volume_urn;
    return STATUS_OK;
  };

  if (!Get("output")->isSet())
    return INVALID_INPUT;

  string output_path = GetArg<TCLAP::ValueArg<string>>("output")->getValue();
  URN output_urn(URN::NewURNFromFilename(output_path));

  // We are allowed to write on the output file.
  if (Get("truncate")->isSet()) {
    std::cout << "Output file " << output_urn.SerializeToString() <<
        " will be truncated.\n";;

    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
  } else {
    resolver.Set(output_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
  };

  AFF4ScopedPtr<AFF4Stream> output_stream = resolver.AFF4FactoryOpen
      <AFF4Stream>(output_urn);

  if (!output_stream) {
    LOG(ERROR) << "Failed to create output file: " <<
        output_urn.SerializeToString();

    return IO_ERROR;
  };

  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
      &resolver, output_stream->urn);

  if (!zip) {
    return IO_ERROR;
  };

  volume_urn = zip->urn;
  output_volume_urn = zip->urn;

  return STATUS_OK;
};


AFF4Status BasicImager::handle_compression() {
  string compression_setting = GetArg<TCLAP::ValueArg<string>>(
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
  };

  std::cout << "Setting compression " << compression_setting.c_str() << "\n";

  return CONTINUE;
};

bool BasicImager::progress_renderer(
    aff4_off_t readptr, ProgressContext &context) {
  bool result = default_progress(readptr, context);

  if (should_abort) {
    std::cout << "\n\nAborted!\n";
    return false;
  };

  return result;
};

void BasicImager::Abort() {
  // Tell everything to wind down.
  should_abort = true;
};


#ifdef _WIN32
// We only allow a wild card in the last component.
vector<string> BasicImager::GlobFilename(string glob) const {
  vector<string> result;
  WIN32_FIND_DATA ffd;
  unsigned int found = glob.find_last_of("/\\");
  string path;

  if (found == string::npos) {
    path = glob;
  } else {
    path = glob.substr(0, found);
  }

  HANDLE hFind = FindFirstFile(glob.c_str(), &ffd);
  if (INVALID_HANDLE_VALUE != hFind) {
    do {
      if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        result.push_back(path + "/" + ffd.cFileName);
      };
    } while (FindNextFile(hFind, &ffd) != 0);
  };
  FindClose(hFind);

  return result;
};
#else

vector<string> BasicImager::GlobFilename(string glob) const {
  vector<string> result;
  result.push_back(glob);

  return result;
};
#endif
