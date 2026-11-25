#include "engine/host_cmd.h"

void NetDataBlockSender_OnCompressError()
{
	Assert(0);
	Error(eDLL_T::SERVER, 0, "LZ4 error compressing data block for client.\n");
}

void NetDataBlockReceiver_OnDecompressError()
{
	Assert(0);

	COM_ExplainDisconnection(true, "LZ4 error decompressing data block from server.\n");
	v_Host_Disconnect(true);
}
