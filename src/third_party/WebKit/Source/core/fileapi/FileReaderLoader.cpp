/*
 * Copyright (C) 2010 Google Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "core/fileapi/FileReaderLoader.h"

#include "core/dom/ScriptExecutionContext.h"
#include "core/fetch/TextResourceDecoder.h"
#include "core/fileapi/Blob.h"
#include "core/fileapi/BlobRegistry.h"
#include "core/fileapi/BlobURL.h"
#include "core/fileapi/FileReaderLoaderClient.h"
#include "core/fileapi/Stream.h"
#include "core/loader/ThreadableLoader.h"
#include "core/platform/network/ResourceRequest.h"
#include "core/platform/network/ResourceResponse.h"
#include "wtf/ArrayBuffer.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefPtr.h"
#include "wtf/Vector.h"
#include "wtf/text/Base64.h"
#include "wtf/text/StringBuilder.h"

using namespace std;

namespace WebCore {

const int defaultBufferLength = 32768;

FileReaderLoader::FileReaderLoader(ReadType readType, FileReaderLoaderClient* client)
    : m_readType(readType)
    , m_client(client)
    , m_urlForReadingIsStream(false)
    , m_isRawDataConverted(false)
    , m_stringResult("")
    , m_variableLength(false)
    , m_bytesLoaded(0)
    , m_totalBytes(0)
    , m_hasRange(false)
    , m_rangeStart(0)
    , m_rangeEnd(0)
    , m_errorCode(FileError::OK)
{
}

FileReaderLoader::~FileReaderLoader()
{
    terminate();
    if (!m_urlForReading.isEmpty()) {
        if (m_urlForReadingIsStream)
            BlobRegistry::unregisterStreamURL(m_urlForReading);
        else
            BlobRegistry::unregisterBlobURL(m_urlForReading);
    }
}

void FileReaderLoader::startForURL(ScriptExecutionContext* scriptExecutionContext, const KURL& url)
{
    // The blob is read by routing through the request handling layer given a temporary public url.
    m_urlForReading = BlobURL::createPublicURL(scriptExecutionContext->securityOrigin());
    if (m_urlForReading.isEmpty()) {
        failed(FileError::SECURITY_ERR);
        return;
    }

    if (m_urlForReadingIsStream)
        BlobRegistry::registerStreamURL(scriptExecutionContext->securityOrigin(), m_urlForReading, url);
    else
        BlobRegistry::registerBlobURL(scriptExecutionContext->securityOrigin(), m_urlForReading, url);

    // Construct and load the request.
    ResourceRequest request(m_urlForReading);
    request.setHTTPMethod("GET");
    if (m_hasRange)
        request.setHTTPHeaderField("Range", String::format("bytes=%d-%d", m_rangeStart, m_rangeEnd));

    ThreadableLoaderOptions options;
    options.sendLoadCallbacks = SendCallbacks;
    options.sniffContent = DoNotSniffContent;
    options.preflightPolicy = ConsiderPreflight;
    options.allowCredentials = AllowStoredCredentials;
    options.crossOriginRequestPolicy = DenyCrossOriginRequests;
    // FIXME: Is there a directive to which this load should be subject?
    options.contentSecurityPolicyEnforcement = DoNotEnforceContentSecurityPolicy;

    if (m_client)
        m_loader = ThreadableLoader::create(scriptExecutionContext, this, request, options);
    else
        ThreadableLoader::loadResourceSynchronously(scriptExecutionContext, request, *this, options);
}

void FileReaderLoader::start(ScriptExecutionContext* scriptExecutionContext, const Blob& blob)
{
    m_urlForReadingIsStream = false;
    startForURL(scriptExecutionContext, blob.url());
}

void FileReaderLoader::start(ScriptExecutionContext* scriptExecutionContext, const Stream& stream, unsigned readSize)
{
    if (readSize > 0) {
        m_hasRange = true;
        m_rangeStart = 0;
        m_rangeEnd = readSize - 1; // End is inclusive so (0,0) is a 1-byte read.
    }

    m_urlForReadingIsStream = true;
    startForURL(scriptExecutionContext, stream.url());
}

void FileReaderLoader::cancel()
{
    m_errorCode = FileError::ABORT_ERR;
    terminate();
}

void FileReaderLoader::terminate()
{
    if (m_loader) {
        m_loader->cancel();
        cleanup();
    }
}

void FileReaderLoader::cleanup()
{
    m_loader = 0;

    // If we get any error, we do not need to keep a buffer around.
    if (m_errorCode) {
        m_rawData = 0;
        m_stringResult = "";
        m_isRawDataConverted = true;
    }
}

void FileReaderLoader::didReceiveResponse(unsigned long, const ResourceResponse& response)
{
    if (response.httpStatusCode() != 200) {
        failed(httpStatusCodeToErrorCode(response.httpStatusCode()));
        return;
    }

    unsigned long long length = response.expectedContentLength();

    // A value larger than INT_MAX means that the content length wasn't
    // specified, so the buffer will need to be dynamically grown.
    if (length > INT_MAX) {
        m_variableLength = true;
        if (m_hasRange)
            length = 1 + m_rangeEnd - m_rangeStart;
        else
            length = defaultBufferLength;
    }

    // Check that we can cast to unsigned since we have to do
    // so to call ArrayBuffer's create function.
    // FIXME: Support reading more than the current size limit of ArrayBuffer.
    if (length > numeric_limits<unsigned>::max()) {
        failed(FileError::NOT_READABLE_ERR);
        return;
    }

    ASSERT(!m_rawData);
    m_rawData = ArrayBuffer::create(static_cast<unsigned>(length), 1);

    if (!m_rawData) {
        failed(FileError::NOT_READABLE_ERR);
        return;
    }

    m_totalBytes = static_cast<unsigned>(length);

    if (m_client)
        m_client->didStartLoading();
}

void FileReaderLoader::didReceiveData(const char* data, int dataLength)
{
    ASSERT(data);
    ASSERT(dataLength > 0);

    // Bail out if we already encountered an error.
    if (m_errorCode)
        return;

    if (m_readType == ReadByClient) {
        m_bytesLoaded += dataLength;

        if (m_client)
            m_client->didReceiveDataForClient(data, dataLength);
        return;
    }

    int length = dataLength;
    unsigned remainingBufferSpace = m_totalBytes - m_bytesLoaded;
    if (length > static_cast<long long>(remainingBufferSpace)) {
        // If the buffer has hit maximum size, it can't be grown any more.
        if (m_totalBytes >= numeric_limits<unsigned>::max()) {
            failed(FileError::NOT_READABLE_ERR);
            return;
        }
        if (m_variableLength) {
            unsigned long long newLength = m_totalBytes * 2;
            if (newLength > numeric_limits<unsigned>::max())
                newLength = numeric_limits<unsigned>::max();
            RefPtr<ArrayBuffer> newData =
                ArrayBuffer::create(static_cast<unsigned>(newLength), 1);
            memcpy(static_cast<char*>(newData->data()), static_cast<char*>(m_rawData->data()), m_bytesLoaded);

            m_rawData = newData;
            m_totalBytes = static_cast<unsigned>(newLength);
        } else
            length = remainingBufferSpace;
    }

    if (length <= 0)
        return;

    memcpy(static_cast<char*>(m_rawData->data()) + m_bytesLoaded, data, length);
    m_bytesLoaded += length;

    m_isRawDataConverted = false;

    if (m_client)
        m_client->didReceiveData();
}

void FileReaderLoader::didFinishLoading(unsigned long, double)
{
    if (m_readType != ReadByClient && m_variableLength && m_totalBytes > m_bytesLoaded) {
        RefPtr<ArrayBuffer> newData = m_rawData->slice(0, m_bytesLoaded);

        m_rawData = newData;
        m_isRawDataConverted = false;

        m_totalBytes = m_bytesLoaded;
    }
    cleanup();
    if (m_client)
        m_client->didFinishLoading();
}

void FileReaderLoader::didFail(const ResourceError&)
{
    // If we're aborting, do not proceed with normal error handling since it is covered in aborting code.
    if (m_errorCode == FileError::ABORT_ERR)
        return;

    failed(FileError::NOT_READABLE_ERR);
}

void FileReaderLoader::failed(FileError::ErrorCode errorCode)
{
    m_errorCode = errorCode;
    cleanup();
    if (m_client)
        m_client->didFail(m_errorCode);
}

FileError::ErrorCode FileReaderLoader::httpStatusCodeToErrorCode(int httpStatusCode)
{
    switch (httpStatusCode) {
    case 403:
        return FileError::SECURITY_ERR;
    case 404:
        return FileError::NOT_FOUND_ERR;
    default:
        return FileError::NOT_READABLE_ERR;
    }
}

PassRefPtr<ArrayBuffer> FileReaderLoader::arrayBufferResult() const
{
    ASSERT(m_readType == ReadAsArrayBuffer);

    // If the loading is not started or an error occurs, return an empty result.
    if (!m_rawData || m_errorCode)
        return 0;

    // If completed, we can simply return our buffer.
    if (isCompleted())
        return m_rawData;

    // Otherwise, return a copy.
    return m_rawData->slice(0, m_bytesLoaded);
}

String FileReaderLoader::stringResult()
{
    ASSERT(m_readType != ReadAsArrayBuffer && m_readType != ReadAsBlob && m_readType != ReadByClient);

    // If the loading is not started or an error occurs, return an empty result.
    if (!m_rawData || m_errorCode)
        return m_stringResult;

    // If already converted from the raw data, return the result now.
    if (m_isRawDataConverted)
        return m_stringResult;

    switch (m_readType) {
    case ReadAsArrayBuffer:
        // No conversion is needed.
        break;
    case ReadAsBinaryString:
        m_stringResult = String(static_cast<const char*>(m_rawData->data()), m_bytesLoaded);
        m_isRawDataConverted = true;
        break;
    case ReadAsText:
        convertToText();
        break;
    case ReadAsDataURL:
        // Partial data is not supported when reading as data URL.
        if (isCompleted())
            convertToDataURL();
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    return m_stringResult;
}

void FileReaderLoader::convertToText()
{
    m_isRawDataConverted = true;

    if (!m_bytesLoaded) {
        m_stringResult = "";
        return;
    }

    // Decode the data.
    // The File API spec says that we should use the supplied encoding if it is valid. However, we choose to ignore this
    // requirement in order to be consistent with how WebKit decodes the web content: always has the BOM override the
    // provided encoding.
    // FIXME: consider supporting incremental decoding to improve the perf.
    StringBuilder builder;
    if (!m_decoder)
        m_decoder = TextResourceDecoder::create("text/plain", m_encoding.isValid() ? m_encoding : UTF8Encoding());
    builder.append(m_decoder->decode(static_cast<const char*>(m_rawData->data()), m_bytesLoaded));

    if (isCompleted())
        builder.append(m_decoder->flush());

    m_stringResult = builder.toString();
}

void FileReaderLoader::convertToDataURL()
{
    m_isRawDataConverted = true;

    StringBuilder builder;
    builder.append("data:");

    if (!m_bytesLoaded) {
        m_stringResult = builder.toString();
        return;
    }

    builder.append(m_dataType);
    builder.append(";base64,");

    Vector<char> out;
    base64Encode(static_cast<const char*>(m_rawData->data()), m_bytesLoaded, out);
    out.append('\0');
    builder.append(out.data());

    m_stringResult = builder.toString();
}

bool FileReaderLoader::isCompleted() const
{
    return m_bytesLoaded == m_totalBytes;
}

void FileReaderLoader::setEncoding(const String& encoding)
{
    if (!encoding.isEmpty())
        m_encoding = WTF::TextEncoding(encoding);
}

} // namespace WebCore
