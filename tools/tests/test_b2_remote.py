from b2_remote import check_key, parse_session_listing, remote_path

BUCKET_ID = "4a48fe8875c6214145260818"
BUCKET_NAME = "nocturne-7k3q9x"
READ_ONLY_CAPABILITIES = ["listBuckets", "listFiles", "readFiles"]

def authorize_response(capabilities, buckets, name_prefix=None):
    return {
        "accountId": "0123456789ab",
        "apiInfo": {
            "storageApi": {
                "allowed": {
                    "buckets": buckets,
                    "capabilities": capabilities,
                    "namePrefix": name_prefix,
                },
                "apiUrl": "https://api005.backblazeb2.com",
                "downloadUrl": "https://f005.backblazeb2.com",
            },
        },
        "authorizationToken": "4_0055d1e5b16bd380000000000_01b2c3d4_acct_token=",
    }

def one_bucket():
    return [{"id": BUCKET_ID, "name": BUCKET_NAME}]

def test_a_read_only_one_bucket_key_passes():
    check = check_key(authorize_response(READ_ONLY_CAPABILITIES, one_bucket()))

    assert check.problems == []
    assert check.bucket_name == BUCKET_NAME
    assert check.capabilities == READ_ONLY_CAPABILITIES

def test_extra_read_capabilities_are_allowed():
    capabilities = READ_ONLY_CAPABILITIES + ["readBucketInfo", "readFileRetentions"]
    check = check_key(authorize_response(capabilities, one_bucket()))

    assert check.problems == []

def test_the_device_key_is_refused():
    check = check_key(authorize_response(["writeFiles"], one_bucket()))

    assert "the key is not read-only: it has writeFiles" in check.problems
    assert "the key lacks listFiles" in check.problems
    assert "the key lacks readFiles" in check.problems

def test_a_key_that_can_delete_is_refused():
    capabilities = READ_ONLY_CAPABILITIES + ["deleteFiles"]
    check = check_key(authorize_response(capabilities, one_bucket()))

    assert check.problems == ["the key is not read-only: it has deleteFiles"]

def test_a_key_that_can_share_is_refused():
    capabilities = READ_ONLY_CAPABILITIES + ["shareFiles"]
    check = check_key(authorize_response(capabilities, one_bucket()))

    assert check.problems == ["the key is not read-only: it has shareFiles"]

def test_a_key_without_list_buckets_is_refused():
    check = check_key(authorize_response(["listFiles", "readFiles"], one_bucket()))

    assert check.problems == ["the key lacks listBuckets"]

def test_an_unrestricted_key_is_refused():
    for buckets in (None, []):
        check = check_key(authorize_response(READ_ONLY_CAPABILITIES, buckets))

        assert check.problems == ["the key is not restricted to a bucket"]
        assert check.bucket_name is None

def test_a_two_bucket_key_is_refused():
    buckets = one_bucket() + [{"id": "5b59", "name": "other"}]
    check = check_key(authorize_response(READ_ONLY_CAPABILITIES, buckets))

    assert check.problems == ["the key covers 2 buckets, not one"]

def test_a_bucket_without_a_name_is_refused():
    check = check_key(authorize_response(READ_ONLY_CAPABILITIES, [{"id": BUCKET_ID, "name": None}]))

    assert check.problems == ["the key's bucket has no name"]

def test_a_name_prefix_is_refused():
    check = check_key(authorize_response(READ_ONLY_CAPABILITIES, one_bucket(), "20260906_2312/"))

    assert check.problems == ["the key only sees names starting with '20260906_2312/'"]

def test_a_v3_response_is_refused():
    response = {
        "apiInfo": {
            "storageApi": {
                "bucketId": BUCKET_ID,
                "bucketName": BUCKET_NAME,
                "capabilities": READ_ONLY_CAPABILITIES,
            },
        },
    }
    check = check_key(response)

    assert len(check.problems) == 1
    assert "no apiInfo.storageApi.allowed" in check.problems[0]

def test_missing_capabilities_count_as_none():
    check = check_key(authorize_response(None, one_bucket()))

    assert check.capabilities == []
    assert len(check.problems) == len(READ_ONLY_CAPABILITIES)

def test_session_listing_strips_slashes_and_sorts():
    text = "20261017_0248/\n19700101_0000/\n\n20260906_2312/\n"

    assert parse_session_listing(text) == ["19700101_0000", "20260906_2312", "20261017_0248"]

def test_an_empty_bucket_lists_no_sessions():
    assert parse_session_listing("") == []

def test_remote_path_names_the_bucket():
    assert remote_path("nocturne", BUCKET_NAME) == "nocturne:nocturne-7k3q9x"
