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

#include "config.h"
#include "libaff4.h"

#include <glog/logging.h>
#include "aff4_directory.h"

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>


AFF4ScopedPtr<AFF4Directory> AFF4Directory::NewAFF4Directory(
    DataStore* resolver, URN root_urn) {
    AFF4Directory* result = new AFF4Directory(resolver);

    std::string root_path = root_urn.ToFilename();
    struct stat s;
    XSDString mode;

    resolver->Get(root_urn, AFF4_STREAM_WRITE_MODE, mode);

    // If mode is truncate we need to clear the directory.
    if (mode == "truncate") {
        RemoveDirectory(root_path.c_str());
    }

    if (stat(root_path.c_str(), &s) < 0) {
        if (mode == "truncate" || mode == "append") {
            // Path does not exist. Try to create it.
            if (MkDir(root_path.c_str()) != STATUS_OK) {
                return AFF4ScopedPtr<AFF4Directory>();
            }
        } else {
            LOG(ERROR) << "Directory " << root_path << " does not exist, and "
                       "AFF4_STREAM_WRITE_MODE is not truncate";

            return AFF4ScopedPtr<AFF4Directory>();
        }
    }

    resolver->Set(result->urn, AFF4_TYPE, new URN(AFF4_DIRECTORY_TYPE));
    resolver->Set(result->urn, AFF4_STORED, new URN(root_urn));

    if (result) {
        result->LoadFromURN();
    }

    return resolver->CachePut<AFF4Directory>(result);
}

AFF4ScopedPtr<AFF4Stream> AFF4Directory::CreateMember(URN child) {
    // Check that child is a relative path in our URN.
    std::string relative_path = urn.RelativePath(child);
    if (relative_path == child.SerializeToString()) {
        return AFF4ScopedPtr<AFF4Stream>();
    }

    // Use this filename. Note that since filesystems can not typically represent
    // files and directories as the same path component we can not allow slashes
    // in the filename. Otherwise we will fail to create e.g. stream/0000000 and
    // stream/0000000.index.
    std::string filename = member_name_for_urn(child, urn, false);

    // We are allowed to create any files inside the directory volume.
    resolver->Set(child, AFF4_TYPE, new URN(AFF4_FILE_TYPE));
    resolver->Set(child, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));
    resolver->Set(child, AFF4_DIRECTORY_CHILD_FILENAME, new XSDString(filename));

    // Store the member inside our storage location.
    resolver->Set(child, AFF4_FILE_NAME,
                  new XSDString(root_path + PATH_SEP_STR + filename));

    AFF4ScopedPtr<FileBackedObject> result = resolver->AFF4FactoryOpen<
            FileBackedObject>(child);

    if (!result) {
        return AFF4ScopedPtr<AFF4Stream>();
    }

    MarkDirty();
    children.insert(child.SerializeToString());

    return result.cast<AFF4Stream>();
}


AFF4Status AFF4Directory::LoadFromURN() {
    if (resolver->Get(urn, AFF4_STORED, storage) != STATUS_OK) {
        LOG(ERROR) << "Unable to find storage for AFF4Directory " <<
                   urn.SerializeToString();

        return NOT_FOUND;
    }

    // We need to get the URN of the container before we can process anything.
    AFF4ScopedPtr<AFF4Stream> desc = resolver->AFF4FactoryOpen<AFF4Stream>(
                                         storage.Append(AFF4_CONTAINER_DESCRIPTION));

    if (desc.get()) {
        std::string urn_string = desc->Read(1000);

        if (urn.SerializeToString() != urn_string) {
            resolver->DeleteSubject(urn);
            urn.Set(urn_string);

            // Set these triples with the new URN so we know how to open it.
            resolver->Set(urn, AFF4_TYPE, new URN(AFF4_DIRECTORY_TYPE));
            resolver->Set(urn, AFF4_STORED, new URN(storage));

            LOG(INFO) << "AFF4Directory volume found: " <<
                      urn.SerializeToString();
        }
    }

    // The actual filename for the root directory.
    root_path = storage.ToFilename();

    // Try to load the RDF metadata file from the storage.
    AFF4ScopedPtr<AFF4Stream> turtle_stream = resolver->AFF4FactoryOpen<
            AFF4Stream>(storage.Append(AFF4_CONTAINER_INFO_TURTLE));

    // Its ok if the information file does not exist - we will make it later.
    if (!turtle_stream) {
        return STATUS_OK;
    }

    AFF4Status res = resolver->LoadFromTurtle(*turtle_stream);

    if (res != STATUS_OK) {
        LOG(ERROR) << "Unable to parse " <<
                   turtle_stream->urn.SerializeToString();
        return IO_ERROR;
    }

    // Find all the contained objects and adjust their filenames.
    XSDString child_filename;

    for (auto subject : resolver->SelectSubjectsByPrefix(urn)) {
        if (resolver->Get(subject, AFF4_DIRECTORY_CHILD_FILENAME,
                          child_filename) == STATUS_OK) {
            resolver->Set(subject, AFF4_FILE_NAME, new XSDString(
                              root_path + PATH_SEP_STR + child_filename.SerializeToString()));
        }
    }

    return STATUS_OK;
}


AFF4Status AFF4Directory::Flush() {
    if (IsDirty()) {
        // Flush all children before us. This ensures that metadata is fully
        // generated for each child.
        for (auto it : children) {
            AFF4ScopedPtr<AFF4Object> obj = resolver->CacheGet<AFF4Object>(it);
            if (obj.get()) {
                obj->Flush();
            }
        }

        // Mark the container with its URN
        AFF4ScopedPtr<AFF4Stream> desc = CreateMember(urn.Append(AFF4_CONTAINER_DESCRIPTION));

        if (!desc) {
            return IO_ERROR;
        }

        desc->Truncate();
        desc->Write(urn.SerializeToString());
        desc->Flush();    // Flush explicitly since we already flushed above.

        // Dump the resolver into the zip file.
        AFF4ScopedPtr<AFF4Stream> turtle_stream = CreateMember(
                    urn.Append(AFF4_CONTAINER_INFO_TURTLE));

        if (!turtle_stream) {
            return IO_ERROR;
        }

        // Overwrite the old turtle file with the newer data.
        turtle_stream->Truncate();
        resolver->DumpToTurtle(*turtle_stream, urn);
        turtle_stream->Flush();

#ifdef AFF4_HAS_LIBYAML_CPP
        AFF4ScopedPtr<AFF4Stream> yaml_segment = CreateMember(
                    urn.Append(AFF4_CONTAINER_INFO_YAML));

        if (!yaml_segment) {
            return IO_ERROR;
        }

        resolver->DumpToYaml(*yaml_segment);
        yaml_segment->Flush();
#endif
    }

    return AFF4Volume::Flush();
}


bool AFF4Directory::IsDirectory(const URN& urn) {
    std::string filename = urn.ToFilename();
    return AFF4Directory::IsDirectory(filename);
}

#ifdef _WIN32

// Recursively deletes the specified directory and all its contents
//   path: Absolute path of the directory that will be deleted

//   The path must not be terminated with a path separator.
AFF4Status AFF4Directory::RemoveDirectory(const string& path) {
    WIN32_FIND_DATA ffd;
    string search_str = path + PATH_SEP_STR + "*";
    HANDLE hFind = INVALID_HANDLE_VALUE;

    hFind = FindFirstFile(search_str.c_str(), &ffd);
    if (INVALID_HANDLE_VALUE == hFind) {
        return IO_ERROR;
    }

    // List all the files in the directory with some info about them.
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (ffd.cFileName[0] == '.') {
                continue;
            }

            // Recurse into the subdir.
            AFF4Status result = AFF4Directory::RemoveDirectory(
                                    path + PATH_SEP_STR + ffd.cFileName);
            if (result != STATUS_OK) {
                return result;
            }
        } else {
            string filename = path + PATH_SEP_STR + ffd.cFileName;
            LOG(INFO) << "Deleting file " << filename;
            if (!::DeleteFile(filename.c_str())) {
                LOG(ERROR) << "Failed: " << GetLastErrorMessage();
                return IO_ERROR;
            }
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    LOG(INFO) << "Deleting directory " << path;
    if (!::RemoveDirectory(path.c_str())) {
        LOG(ERROR) << "Failed: " << GetLastErrorMessage();
        FindClose(hFind);
        return IO_ERROR;
    }

    FindClose(hFind);
    return STATUS_OK;
}

AFF4Status AFF4Directory::MkDir(const string& path) {
    LOG(INFO) << "MkDir " << path;

    if (!CreateDirectory(path.c_str(), nullptr)) {
        DWORD res = GetLastError();
        if (res == ERROR_ALREADY_EXISTS) {
            return STATUS_OK;
        }

        LOG(ERROR) << "Cant create directory " << path << ": " <<
                   GetLastErrorMessage();
        return IO_ERROR;
    }

    return STATUS_OK;
}

bool AFF4Directory::IsDirectory(const string& filename) {
    DWORD dwAttrib = GetFileAttributes(filename.c_str());

    bool result = (dwAttrib != INVALID_FILE_ATTRIBUTES &&
                   (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

    // If the URN ends with a / and we have permissions to create it, it is a
    // directory.
    char last = *(filename.rbegin());
    result |= (last == '/' || last == '\\');

    LOG(INFO) << "IsDirectory " << filename << ": " << result;

    return result;
}

#else

AFF4Status AFF4Directory::MkDir(const std::string& path) {
    if (mkdir(path.c_str(), 0777) < 0) {
        LOG(ERROR) << "Failed to create directory " << path.c_str() <<
                   " : " << GetLastErrorMessage();
        return IO_ERROR;
    }
    return STATUS_OK;
}

bool AFF4Directory::IsDirectory(const std::string& filename) {
    DIR* dir = opendir(filename.c_str());
    if (!dir) {
        // If the URN ends with a / and we have permissions to create it, it is a
        // directory.
        if (*(filename.rbegin()) == PATH_SEP) {
            return true;
        }

        return false;
    }

    closedir(dir);
    return true;
}


// Recursively remove all files and subdirectories in the directory.
AFF4Status AFF4Directory::RemoveDirectory(const std::string& path) {
    DIR* dir;
    std::string dirname = path;

    if (*dirname.rbegin() != PATH_SEP) {
        dirname += PATH_SEP_STR;
    }

    /* Open directory stream */
    dir = opendir(dirname.c_str());
    if (dir != nullptr) {
        struct dirent* ent;

        /* Print all files and directories within the directory */
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') {
                continue;
            }

            std::string full_path = dirname + ent->d_name;

            switch (ent->d_type) {
                case DT_REG: {
                    LOG(INFO) << "Removing file " << full_path.c_str();
                    unlink(full_path.c_str());
                }
                break;

                case DT_DIR: {
                    AFF4Status result = AFF4Directory::RemoveDirectory(full_path);
                    LOG(INFO) << "Removing directory " << full_path;
                    rmdir(full_path.c_str());
                    closedir(dir);
                    return result;
                }
                break;
                default:
                    break;
            }
        }
        closedir(dir);
    }

    return STATUS_OK;
}

#endif


static AFF4Registrar<AFF4Directory> r1(AFF4_DIRECTORY_TYPE);

void aff4_directory_init() {}
