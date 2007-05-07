/*** Generic types ***********************************************************/

typedef unsigned ParcelVer;
typedef opaque Hash<64>;
typedef opaque EncryptedKeyRecord<>;
typedef opaque EncryptedKeyRootRecord<>;
typedef unsigned Timestamp;  /* XXX */
typedef string String<>;
typedef opaque ChunkData<131072>;

enum ChunkPlane {
	PLANE_DISK = 0,
	PLANE_MEMORY = 1
};

enum Compression {
	COMPRESS_NONE = 0,
	COMPRESS_ZLIB = 1,
	COMPRESS_LZF = 2
};

enum Encryption {
	ENCRYPT_NONE_SHA1 = 0,
	ENCRYPT_BLOWFISH_SHA1 = 1,
	ENCRYPT_AES_SHA1 = 2
};

struct ChunkID {
	unsigned	cid;
	ChunkPlane	plane;
};

typedef ChunkID ChunkArray<>;

struct DateRange {
	Timestamp	start;
	Timestamp	stop;
};

struct KeyRecord {
	Hash		key;
	Compression	compress;
	Encryption	encrypt;
};

struct KeyRootRecord {
	Hash		key;
};

/** Define:	reply **/
enum Status {
	STATUS_OK,
	STATUS_CONTINUE,		/* authentication incomplete */
	STATUS_NOT_SUPPORTED,
	STATUS_TLS_REQUIRED,
	STATUS_NOT_AUTHENTICATED,
	STATUS_AUTH_FAILED,
	STATUS_PARCEL_LOCKED,
	STATUS_NO_SUCH_PARCEL,
	STATUS_NO_SUCH_VERSION,
	STATUS_NO_SUCH_RECORD,
	STATUS_NO_DATA,
	STATUS_DATA_INCOMPLETE,	/* XXX redundant? */
	STATUS_QUOTA_EXCEEDED
};

/*** Startup and authentication messages *************************************/

enum SupportedProtocolVersions {
	VER_V0 = 0
};

struct ClientHello {
	String		software;
	String		ver;
	unsigned	protocolSupportBitmap;
};

struct ServerHello {
	String		software;
	String		ver;
	String		*banner;
	SupportedProtocolVersions	protocolChosen;
	bool		tlsSupported;
	bool		tlsRequired;
	String		authTypes;		/* space-separated */ /* XXX */
};

typedef opaque AuthData<>;

struct Authenticate {
	String		method;
	AuthData	data;
};

struct AuthReply {
	Status		status;
	String		*message;
	AuthData	data;
};

/* XXX should password changing be part of the protocol?  could provide a
   "get password change URL" message for user experience. */

/*** Parcel operations *******************************************************/

/* @force can be declined if an open connection currently exists which holds
   the lock? */
struct AcquireLock {
	String		parcel;
	ParcelVer	ver;
	String		owner;
	String		*comment;
	unsigned	*cookie;
	bool		force;
};

struct LockResult {
	unsigned	cookie;
};

struct ReleaseLock {
	String		parcel;
	unsigned	cookie;
};

/* On checkout, a temporary area for keyring updates is (logically) created
   and is based on the keyring version that was checked out.  Keyring updates
   may then be sent, and may supersede previous keyring updates.  Chunk uploads
   may be made against the keys in the temporary keyring.  Commit will not
   be allowed until every new key has the corresponding chunk data. */

struct ParcelCommit {
	String		parcel;
	String		comment;
};

struct AddParcel {
	String		parcel;
	String		*comment;
	unsigned	chunks;
	EncryptedKeyRootRecord root;
};

struct UpdateParcel {
	String		parcel;
	String		*comment;
};

struct UpdateKeyRoot {
	EncryptedKeyRootRecord root;
};

struct RemoveParcel {
	String		parcel;
	ParcelVer	*ver;
};

/*** ListParcels *************************************************************/

struct ListParcels {
	String		*name;
	DateRange	*history;
};

struct ParcelVersionInfo {
	ParcelVer	ver;
	String		*comment;
	Timestamp	checkin;
	unsigned	chunks;		/* unique chunks */
};

struct ParcelLockInfo {
	ParcelVersionInfo ver;
	String		by;
	Timestamp	at;
	String		*comment;
};

struct ParcelInfo {
	String		name;
	String		*comment;
	ParcelVersionInfo current;
	ParcelLockInfo	*acquired;
	ParcelVersionInfo history<>;
};

typedef ParcelInfo ListParcelsReply<>;

/*** Chunk operations ********************************************************/

enum ChunkLookupBy {
	BY_CHUNK,
	BY_TAG
};

union ChunkLookupKey switch (ChunkLookupBy key) {
case BY_CHUNK:
	ChunkID		chunk;
case BY_TAG:
	Hash		tag;
};

enum WantChunkInfo {
	WANT_CHUNKS = 0,
	WANT_KEY = 1,
	WANT_DATA = 2
};

struct ChunkRequest {
	ChunkLookupKey	by;
	unsigned	WantChunkInfoBits;
};

struct ChunkReply {
	ChunkArray	*chunks;
	EncryptedKeyRecord *keyrec;
	ChunkData	*data;
};

struct KeyUpdate {
	ChunkArray	chunks;
	Hash		tag;
	EncryptedKeyRecord keyrec;
};

struct ChunkUpload {
	Hash		tag;
	ChunkData	data;
};

struct GetMissingData {
	String		parcel;
	unsigned	startIndex;	/* message size limit applies */
	unsigned	*stopIndex;
};

typedef Hash NeedTags<>;

/*** Program *****************************************************************/

#ifndef RPCGEN
program OPENISR {
	version V0 {
		ServerHello hello(ClientHello) = 1;
		void start_tls() = 2;
		AuthReply start_auth(Authenticate) = 3;
		AuthReply auth_step(AuthStep) = 4;
		
		LockResult acquire_lock(AcquireLock) = 5;
		Status release_lock(ReleaseLock) = 6;
		Status parcel_commit(ParcelCommit) = 7;
		Status add_parcel(AddParcel) = 8;
		Status update_parcel(UpdateParcel) = 9;
		Status update_key_root(UpdateKeyRoot) = 10;
		Status remove_parcel(RemoveParcel) = 11;
		ListParcelsReply list_parcels(ListParcels) = 12;
		
		ChunkReply chunk_request(ChunkRequest) = 13;
		Status key_update(KeyUpdate) = 14;
		Status chunk_upload(ChunkUpload) = 15;
		NeedTags get_missing_data(GetMissingData) = 16;
		
		Status ping() = 17;
	} = 0;
} = 23456789;
#endif