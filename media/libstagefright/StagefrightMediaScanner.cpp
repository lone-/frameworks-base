/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "StagefrightMediaScanner"
#include <utils/Log.h>

#include <media/stagefright/StagefrightMediaScanner.h>

#include <media/mediametadataretriever.h>
#include <private/media/VideoFrame.h>

// Sonivox includes
#include <libsonivox/eas.h>

namespace android {

StagefrightMediaScanner::StagefrightMediaScanner()
    : mRetriever(new MediaMetadataRetriever) {
}

StagefrightMediaScanner::~StagefrightMediaScanner() {}

static bool FileHasAcceptableExtension(const char *extension) {
    static const char *kValidExtensions[] = {
        ".mp3", ".mp4", ".m4a", ".3gp", ".3gpp", ".3g2", ".3gpp2",
        ".mpeg", ".ogg", ".mid", ".smf", ".imy", ".wma", ".aac",
        ".wav", ".amr", ".midi", ".xmf", ".rtttl", ".rtx", ".ota",
        ".mkv", ".mka", ".webm", ".ts"
    };
    static const size_t kNumValidExtensions =
        sizeof(kValidExtensions) / sizeof(kValidExtensions[0]);

    for (size_t i = 0; i < kNumValidExtensions; ++i) {
        if (!strcasecmp(extension, kValidExtensions[i])) {
            return true;
        }
    }

    return false;
}

static status_t HandleMIDI(
        const char *filename, MediaScannerClient *client) {
    // get the library configuration and do sanity check
    const S_EAS_LIB_CONFIG* pLibConfig = EAS_Config();
    if ((pLibConfig == NULL) || (LIB_VERSION != pLibConfig->libVersion)) {
        LOGE("EAS library/header mismatch\n");
        return UNKNOWN_ERROR;
    }
    EAS_I32 temp;

    // spin up a new EAS engine
    EAS_DATA_HANDLE easData = NULL;
    EAS_HANDLE easHandle = NULL;
    EAS_RESULT result = EAS_Init(&easData);
    if (result == EAS_SUCCESS) {
        EAS_FILE file;
        file.path = filename;
        file.fd = 0;
        file.offset = 0;
        file.length = 0;
        result = EAS_OpenFile(easData, &file, &easHandle);
    }
    if (result == EAS_SUCCESS) {
        result = EAS_Prepare(easData, easHandle);
    }
    if (result == EAS_SUCCESS) {
        result = EAS_ParseMetaData(easData, easHandle, &temp);
    }
    if (easHandle) {
        EAS_CloseFile(easData, easHandle);
    }
    if (easData) {
        EAS_Shutdown(easData);
    }

    if (result != EAS_SUCCESS) {
        return UNKNOWN_ERROR;
    }

    char buffer[20];
    sprintf(buffer, "%ld", temp);
    if (!client->addStringTag("duration", buffer)) return UNKNOWN_ERROR;

    return OK;
}

status_t StagefrightMediaScanner::processFile(
        const char *path, const char *mimeType,
        MediaScannerClient &client) {
    LOGV("processFile '%s'.", path);

    client.setLocale(locale());
    client.beginFile();

    const char *extension = strrchr(path, '.');

    if (!extension) {
        return UNKNOWN_ERROR;
    }

    if (!FileHasAcceptableExtension(extension)) {
        client.endFile();

        return UNKNOWN_ERROR;
    }

    if (!strcasecmp(extension, ".mid")
            || !strcasecmp(extension, ".smf")
            || !strcasecmp(extension, ".imy")
            || !strcasecmp(extension, ".midi")
            || !strcasecmp(extension, ".xmf")
            || !strcasecmp(extension, ".rtttl")
            || !strcasecmp(extension, ".rtx")
            || !strcasecmp(extension, ".ota")) {
        status_t status = HandleMIDI(path, &client);
        if (status != OK) {
            return status;
        }
    } else if (mRetriever->setDataSource(path) == OK
            && mRetriever->setMode(
                METADATA_MODE_METADATA_RETRIEVAL_ONLY) == OK) {
        const char *value;
        if ((value = mRetriever->extractMetadata(
                        METADATA_KEY_MIMETYPE)) != NULL) {
            client.setMimeType(value);
        }

        struct KeyMap {
            const char *tag;
            int key;
        };
        static const KeyMap kKeyMap[] = {
            { "tracknumber", METADATA_KEY_CD_TRACK_NUMBER },
            { "discnumber", METADATA_KEY_DISC_NUMBER },
            { "album", METADATA_KEY_ALBUM },
            { "artist", METADATA_KEY_ARTIST },
            { "albumartist", METADATA_KEY_ALBUMARTIST },
            { "composer", METADATA_KEY_COMPOSER },
            { "genre", METADATA_KEY_GENRE },
            { "title", METADATA_KEY_TITLE },
            { "year", METADATA_KEY_YEAR },
            { "duration", METADATA_KEY_DURATION },
            { "writer", METADATA_KEY_WRITER },
        };
        static const size_t kNumEntries = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

        for (size_t i = 0; i < kNumEntries; ++i) {
            const char *value;
            if ((value = mRetriever->extractMetadata(kKeyMap[i].key)) != NULL) {
                client.addStringTag(kKeyMap[i].tag, value);
            }
        }
    }

    client.endFile();

    return OK;
}

char *StagefrightMediaScanner::extractAlbumArt(int fd) {
    LOGV("extractAlbumArt %d", fd);

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        return NULL;
    }
    lseek(fd, 0, SEEK_SET);

    if (mRetriever->setDataSource(fd, 0, size) == OK
            && mRetriever->setMode(
                METADATA_MODE_FRAME_CAPTURE_ONLY) == OK) {
        sp<IMemory> mem = mRetriever->extractAlbumArt();

        if (mem != NULL) {
            MediaAlbumArt *art = static_cast<MediaAlbumArt *>(mem->pointer());

            char *data = (char *)malloc(art->mSize + 4);
            *(int32_t *)data = art->mSize;
            memcpy(&data[4], &art[1], art->mSize);

            return data;
        }
    }

    return NULL;
}

}  // namespace android
