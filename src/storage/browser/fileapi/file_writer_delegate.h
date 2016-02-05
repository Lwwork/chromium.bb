// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BROWSER_FILEAPI_FILE_WRITER_DELEGATE_H_
#define STORAGE_BROWSER_FILEAPI_FILE_WRITER_DELEGATE_H_

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/file_stream.h"
#include "net/base/io_buffer.h"
#include "net/url_request/url_request.h"
#include "storage/browser/storage_browser_export.h"

namespace storage {

class FileStreamWriter;
enum class FlushPolicy;

class STORAGE_EXPORT FileWriterDelegate : public net::URLRequest::Delegate {
 public:
  enum WriteProgressStatus {
    SUCCESS_IO_PENDING,
    SUCCESS_COMPLETED,
    ERROR_WRITE_STARTED,
    ERROR_WRITE_NOT_STARTED,
  };

  typedef base::Callback<void(base::File::Error result,
                              int64 bytes,
                              WriteProgressStatus write_status)>
      DelegateWriteCallback;

  FileWriterDelegate(scoped_ptr<FileStreamWriter> file_writer,
                     FlushPolicy flush_policy);
  ~FileWriterDelegate() override;

  void Start(scoped_ptr<net::URLRequest> request,
             const DelegateWriteCallback& write_callback);

  // Cancels the current write operation.  This will synchronously or
  // asynchronously call the given write callback (which may result in
  // deleting this).
  void Cancel();

  void OnReceivedRedirect(net::URLRequest* request,
                          const net::RedirectInfo& redirect_info,
                          bool* defer_redirect) override;
  void OnAuthRequired(net::URLRequest* request,
                      net::AuthChallengeInfo* auth_info) override;
  void OnCertificateRequested(
      net::URLRequest* request,
      net::SSLCertRequestInfo* cert_request_info) override;
  void OnSSLCertificateError(net::URLRequest* request,
                             const net::SSLInfo& ssl_info,
                             bool fatal) override;
  void OnResponseStarted(net::URLRequest* request) override;
  void OnReadCompleted(net::URLRequest* request, int bytes_read) override;

 private:
  void OnGetFileInfoAndStartRequest(
      scoped_ptr<net::URLRequest> request,
      base::File::Error error,
      const base::File::Info& file_info);
  void Read();
  void OnDataReceived(int bytes_read);
  void Write();
  void OnDataWritten(int write_response);
  void OnError(base::File::Error error);
  void OnProgress(int bytes_read, bool done);
  void OnWriteCancelled(int status);
  void MaybeFlushForCompletion(base::File::Error error,
                               int bytes_written,
                               WriteProgressStatus progress_status);
  void OnFlushed(base::File::Error error,
                 int bytes_written,
                 WriteProgressStatus progress_status,
                 int flush_error);

  WriteProgressStatus GetCompletionStatusOnError() const;

  DelegateWriteCallback write_callback_;
  scoped_ptr<FileStreamWriter> file_stream_writer_;
  base::Time last_progress_event_time_;
  bool writing_started_;
  FlushPolicy flush_policy_;
  int bytes_written_backlog_;
  int bytes_written_;
  int bytes_read_;
  scoped_refptr<net::IOBufferWithSize> io_buffer_;
  scoped_refptr<net::DrainableIOBuffer> cursor_;
  scoped_ptr<net::URLRequest> request_;

  base::WeakPtrFactory<FileWriterDelegate> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(FileWriterDelegate);
};

}  // namespace storage

#endif  // STORAGE_BROWSER_FILEAPI_FILE_WRITER_DELEGATE_H_
