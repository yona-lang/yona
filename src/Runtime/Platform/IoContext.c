#include "yona/Runtime/Platform/IoContext.h"

#include "yona/Runtime/Core/Api.h"

#include <stdlib.h>
#include <unistd.h>

static int contextBufferIsManaged(YonaIoOperationKind Kind) {
  switch (Kind) {
  case YonaIoOperationReadFile:
  case YonaIoOperationWriteFile:
  case YonaIoOperationSend:
  case YonaIoOperationReceive:
  case YonaIoOperationReceiveBytes:
  case YonaIoOperationReadFileBytes:
  case YonaIoOperationReadFileDescriptorBytes:
  case YonaIoOperationWriteFileDescriptorBytes:
    return 1;
  case YonaIoOperationAccept:
  case YonaIoOperationConnect:
  case YonaIoOperationWriteFileDescriptorString:
    return 0;
  }
  return 0;
}

void YonaRuntimeIoContextCleanupCancelled(YonaIoContext *Context) {
  if (!Context)
    return;
  if (Context->Buffer) {
    if (contextBufferIsManaged(Context->Kind))
      YonaRuntimeRelease(Context->Buffer);
    else
      free(Context->Buffer);
  }
  if (Context->CloseFileDescriptor && Context->FileDescriptor >= 0)
    close(Context->FileDescriptor);
  free(Context);
}
