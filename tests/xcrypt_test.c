/* Host tests for src/xcrypt.c, against known answers: Juicebox SDK's own
test vectors (MIT, juicebox-systems/juicebox-sdk: the OPRF's, the PIN's
hash, a secret's encryption, a proof's CBOR), and answers made by Python's
hashlib and OpenSSL (BLAKE2s, a Noise NK responder, P-256 and AES-GCM,
HKDF-SHA256, ECDSA). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sodium.h>

#include "xcrypt.h"

static int g_failed;

#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failed++; } } while(0)


/**
 * hex into bytes; how many
 **/
static size_t
unhex(const char *hex, uint8_t *out, size_t size) {
  size_t n = 0;

  while(hex[0] && hex[1] && n < size) {
    unsigned v;
    sscanf(hex, "%2x", &v);
    out[n++] = (uint8_t)v;
    hex += 2;
  }
  return n;
}


static int
same_hex(const uint8_t *bytes, size_t len, const char *hex) {
  uint8_t want[512];

  return strlen(hex) == 2 * len && unhex(hex, want, sizeof want) == len && !memcmp(bytes, want, len);
}


/*
 * Random bytes the test chooses: what is queued, then a counter.
 */
static uint8_t g_queue[256];
static size_t g_queued, g_taken;
static uint8_t g_counter;

static void
test_random(void *buf, size_t len) {
  uint8_t *p = buf;

  for(size_t i=0; i<len; i++) {
    p[i] = g_taken < g_queued ? g_queue[g_taken++] : g_counter++;
  }
}


static void
queue_hex(const char *hex) {
  g_queued = unhex(hex, g_queue, sizeof g_queue);
  g_taken = 0;
}


/*
 * Answers of Python (see the comment at the top).
 */
static const char B2S_ABC[] = "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982";
static const char B2S_EMPTY_KEYED[] = "48a8997da407876b3d79c0d92325ad3b89cbb754d86ab71aee047ad345fd2c49";
static const char B2S_LONG_KEYED16[] = "2b92a2abef5cce020941963f87e97685";
static const char B2S_64[] = "56f34e8b96557e90c1f24b52d0c89d51086acf1b00f634cf1dde9233b8eaaa3e";
static const char B2S_65[] = "1b53ee94aaf34e4b159d48de352c7f0661d0a40edff95a0b1639b4090e974472";
static const char NOISE_RS_SEED[] = "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f";
static const char NOISE_E_SEED[] = "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f";
static const char NOISE_RE_PUB[] = "493e82fc74464a59268817623d2053c5eb8e2cc4a988b4fee179ec6b010d531d";
static const char NOISE_E_PUB[] = "675dd574ed7789310b3d2e7681f3790b466c773b1521fecf36577958371ea52f";
static const char NOISE_CT1[] = "c180c32f6085ab4906ea0bdd9961cb45fd8cc9a899f268c76a4d";
static const char NOISE_CT2[] = "197463d6538091d71bd90ddf6e6fc31b091630a2b74917de33743de84b396afac3fc";
static const char NOISE_T1[] = "a7d93e3f8d56d2e934e3c9b7599eab13c10a6e5205";
static const char NOISE_T2[] = "7a31e0c7587b2c49a7d117d3b864643e75f29b39c3";
static const char NOISE_T3[] = "3223792a26ec452a98f71d3d801071df01f0da37f9bf";
static const char ECIES_IDENTITY[] = "01d2c3b4a59687ff00112233445566778899aabbccddeeff0102030405060708";
static const char ECIES_IDENTITY_PUB[] = "04c95339c7b818e2acfaa1e7b930772898c59c05e50ea5dec75b31db460b5fcfd3a12fdef36f64504a5c6bdcfdb4f23bcf1460fbae4fd836d6d411bdeeba6be61e";
static const char ECIES_WRAPPED[] = "04f28ab7f9bf45373fbd0d9b20fb314a7177db2e14acebdd018cdceeda3192a7d4dbefa289f69cd13f4ac9ab631148e6c66a4cf30d51e432a55295742a31ef65661e8794be8777ff53da5197c6591636b22b8620bbf4190cad25fb7952d00e6819b0b0602b38b24634862d91b9f5b26b2c";
static const char ECIES_KEY[] = "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf";
static const char MESSAGE_KEY_V1[] = "aaa406ab010214b1c221d06389ae1d79e9f76efdd934643a1afb90c6050c1d46";
static const char ECDSA_MSG[] = "4d6573736167654372656174654576656e742c61316232633364342d303030302d343030302d383030302d3132333435363738396162632c313233343536373839302c313233343536373839302d3938373635343332312c312c633256686247566b";
static const char ECDSA_SIG[] = "de20d1f719d55a922ffe71b1b3d89b95f3c0e141f3b436c41ff3ba81a0ffd00fa8685e1b488698ce15866f7c56b0f9ae54ee14ef9b55e00fa074350d85bd643f";

// X's SDK (chat-xdk, MIT, tests/fixtures/sdk_vectors.json): a signature of
// version 10 over a message whose contents' encryption version is 1, and
// the signer's key as its SPKI (signature_v10_cev1_b64 and _public_key_b64,
// here in hex); checked with OpenSSL too
static const char SDK_SIGNED[] = "MessageCreateEvent,11111111-2222-3333-4444-555555555555,1491585161162473473,"
  "2:1491585161162473473,5,SGVsbG8,1,signature_version=10";
static const char SDK_SIGNED_NULL[] = "MessageCreateEvent,11111111-2222-3333-4444-555555555555,1491585161162473473,"
  "2:1491585161162473473,5,SGVsbG8,null,signature_version=10";
static const char SDK_SPKI[] = "3059301306072a8648ce3d020106082a8648ce3d03010703420004faddfae950e72ec6626e8f26598c6f3d"
  "00ee00b40fa52c9d8909d1e8682f94759a2f686316e1a107a6e4c2a3a040c8292cd9ad4dde2c3d56978f122434b98a0e";
static const char SDK_SIG[] = "5b9add664d6ad773817d5092c3269617e8d34a8b5c4b45eb69edb8a8ec67b6ba"
  "dd6bcd7e06778a4fc6f7b3464d1ce45a15df909f371097360d0d24c5e6f8e7f4";


/*
 * Juicebox SDK's vectors: rust/oprf/src/test_vectors.json (each seed
 * of 64 bytes reduced into a scalar), rust/sdk/src/pin.rs,
 * rust/sdk/src/types.rs, rust/oprf/src/dleq.rs.
 */
typedef struct oprf_vector {
  const char *input, *private_seed, *blinding_seed, *beta_t_seed;
  const char *private_key, *public_key, *blinding, *blinded_input, *blinded_output;
  const char *proof_c, *proof_beta_z, *output;
} oprf_vector_t;

static const oprf_vector_t g_oprf[] = {
  {"840841056a7b40f363df5a25827a",
   "7319eafd6f8a54a59bb9d450a4cbfc04a6f0bb42797d58fc808e8514b31423e0c81b0f143403a175210497ae2090596b34a6476d602317f5c1ecda80f82050f0",
   "0f0b48a38ad125109d3888c6fc57232a5afc0d7bbf7013c7afd170385d4e47631522b62cc117592337758d86e6a1eaf89eae9afdb16c0027dbb8830d50d88f77",
   "15bb1b0499d15ace9f3159aa3508a88815a2ff6519a1807b7441f93adc5f5c9ae973f29f56d93073dd9149e44d7942cbefc8df60b2ac98d075760af694ea817e",
   "cca1a0304b113ec01cafa2545c0428497fd65a4924b4697033f5c19aaec2ac0a",
   "9e2bc4e246e540092324937ed33fd01caf0297137e35345c32ecf49e87e35056",
   "36fce774182f86c3d373bf632e7056e92371a49fbe27b01ef9d2688605446703",
   "a8767323a469385742eb85b73a3d51372f4e15d336f72567eb12d3410fa6815c",
   "5020caea110ad8333a059d848b33f5607c56eca6c93df929999df5362ed00c63",
   "d1b6200350187462bc499b9761b960560d300f9b03873a3ba8bb31b68e13150e",
   "a3be5bc291c6cc07c86de7b6e0d12a49475663bd4b4805f7a535f96900fa7802",
   "0c12ee3f037370f7c9cf203d144f3653c96174bfbfd63b6c07d67e9a99e4e59953fbfa90232dff216ea2290e915fb8f2818be91f81902994aa50cf9aa77ed843"},
  {"4031",
   "81a5851536ddf8ab2e406cdfa4212239bb9fa70b3371fc210d34989c9021b73502e7ba25204359bda3a7acb1b5d078c9b3591313e30198301d26d2293520c47c",
   "f162c59f57c748bffa0d7e60eab204fb43c667b144219e5164602ef34c190d8ac91bb3337601ec3391a1b04583d768909c716035e955ba5012506e271279a6f8",
   "e3338a037375a11171895585f670ea0a2a195d99f60c0d75a649ed565364b3976eb10bce2dd4c8eed0ffc0597de08e879142590faa87cee2be4d92419090776a",
   "b93aae4cdc5d28f51aedd681218f1dab4ff3a4c1836234a5e291af12794e3c0b",
   "70fd2bd8bc7a9ee328d366d0a97d696a30b1a369663afa43f45de26280300f49",
   "33cafe98e426b123375558e5f8da85bb89be38af5f7c4461f790328459e71305",
   "2a9e7fc1fd010bde24673255559f21bafcf83221251947ef9f497e15b3190c0b",
   "26330cde20259f5f134e6d3291a44e23d1762be006e520008aa173aa5e0e6826",
   "ff6fbb60b5d9ea65c34febdcd73a25181f7e25ec818b8cef101623eaa028bc02",
   "d119a7c682324f6fd4e79aaba1c32e5dd494626d5d115fd2a319bccd474d7f04",
   "bf283a626e65927026c4cfe9e55250b252fc8a0883cd307cc463d498991372584b8b689697d36366a22f0fe10be92309e1a17bea37360205d0448cd26a71f3ac"},
  {"c5523613f851591b30b74d",
   "5c1ed0f935d10d8927262ec850750c9dd2cd68e3dd8e20f39ae19e7fdb226a786528e24b8f8ee96846084e72faa161b0172d5495eca19f487d252e2b7525ecf6",
   "3e7308496c6944a9bdf2e4efeb0d068e8a2033bc5072d0b526aa3bafe86a311553c1a4a5266bb0793f57e13e873158e2109130c36121f6e8e49eb4cd37d42f47",
   "e431e5bcfc811c47792b6a0f99117f94786a95b093a7794ed7bcd1b3a6885f707902ed20e37e0c3718394585d45858d51c20620fe9caafd42d29711e941da0e5",
   "9ffd1a9832e13dd942bdd75633453421bcaa8540d377fb37b3a52f921b43010a",
   "a837f11591ba58f358f3fa02124454b3b0bdf3f910e92d1796f2e80a1c33d85d",
   "d3dde93aca36bd0188b4c2335cbfc95b4aefdacd7fc491e89eeb023dfd21d50e",
   "ea1957d1930a99b88b6c0de809e0d7a143226a70f9909865320e177e77bb5121",
   "f499b4eb255f081866c11e52cb2d9de2ed44f97474bdaf8f67af05896de28d79",
   "0f6c67d6c153751ab07830128d5da2e2181c50db8ce995d423e52c99b42a9707",
   "b6aa330aebbc812de855f0d7433902a32b46ffc46b60b2e11e2d0f206ecc4407",
   "eac0d507b41bc5d0b91d60fc6981c30d03b563d5dbb101997f29e7100d91b68431fabe192a9dde0264ca11c3e436f79e472d6725dfab78d97ab41b18c7ae64b1"},
  {"bae87ff1ab8b923784d140",
   "0df34487695148f7be77e7df7e2a3730032cc513a089ce64fb1e32740dd4a7e29e2f3d48e8512edf400d1a53c9f9a4e6f2c95a278948f51b88a9f7d9ca253c99",
   "0bfa7cc2247b58bdae912d8f01527605d8d4fbfa9697d826afea172d8e2489972efb1bed023dc021b5e843d8f1ae9dcf47e301a91b341603c340b494c0c75e25",
   "aaf3435f3e8ea4059f8c690f689ebb516cc57c1b1dace1a1b589ee4b7454678c14a3065441d31664f28165764f81c0b70fae3340305efb856d14a1cc26da3e73",
   "7c9a034c7c801ad0a99380bf679547b5f700c7d9230d11128365d9867637870f",
   "7498964d042f0e5076e95ee98b1aef4fdbff1f22e8aeb5fd59d47a9da2bd4a20",
   "6e4a84414c008c1d3e5735feee4e8de2c0c1a13650bab596287c5a145879a600",
   "508a7c04b5f454a26a28fd0327e930a4568a0748bf6b1733a52199fdb7394948",
   "cabe1c9ea77239ca962aedc91de7e517b46817e09fe93f168f23a19946086e20",
   "2adeba203706de2a59752717b7131a64b8b6a390cc3d852576cc15d06b73ad0e",
   "4ea4ccbf7515c65946cdddfcfb89e9b743a42826a72459e3f69f1208f23ef60a",
   "60f0bb1d4e5086db245705b8d2b539aa80bfd543d6a3bad00167928b3f336cb700e1dead61df5f52d3b199b0c363c9b5b44d814fd7ebafaf4242b6c703c04d4e"},
  {"84bfbaaffa229a",
   "2aecde6a38e7d08a59b80b667cb56f257c1a52e96153aea4c3d167eebdc0973ffa9b64c4b1fce81c0a746eeaf8062afbd219bc5002374d323784e72bad35e166",
   "7e3310c7f1f8fbf69b091e1f0d5cc9671c1ea173ba8619f34d8e0bcf1195c3dbf178a7e9b56ffcf04584d1e0db7293326b9e5cfa5639b9ab14f9a09ad1e4bd75",
   "f254fe2876c75f2330f11e2f4eed52ba7ba5f6f36428149cd42ac7c203b04d17dd2d980c978dc8a796dc7e112f381a75a80b07cc944294bcf5b8832ee386b6aa",
   "ec4c9c7838002be18fb0f55ffb0d5390ca0e99b1fb7ab4421c66df7c93c24d0a",
   "48e7a635f33d81e318bdb4d9931b4ae9cc9a4e2b9d42b3450508bb6a10a8b87b",
   "f0794117f384696a8fa275ce147385f2488c4eff815a26e5cfa01de25f3a7201",
   "beef0925a05bbe3f42f0462a7dc4d889e875f11c0b36a57f646f3ec1b1730425",
   "b65e41e838399e4da006729145bb87d8702332b8a7feda547c075a02ad2cb50a",
   "a48691dc20d9a857b61d224768cb6047a612ecc802b670aa43fdc2faa82fdd0f",
   "3655d3749ba760ea9dfeed66c754064e2cc9bc6aedcca42a5ad92f3506fa1707",
   "727086cf26d0fb65be933eed0412d3dce9d872775479843ae0a545d95c186a2aa74cf44924cffd11a74ed49591bf37ff9b4ae35641106920c850481dff49f386"},
  {"eed551929fc7f375b08af791e8ea",
   "7fa5bdfb790cb7046eac708ca728ae1ddeca9958b637decc02d8a8dcc4834b7ebdb6abae626a55389663259048c69fd15e803d575f4966973d70b2a2e3c8c8d2",
   "dceed3223fc9a6b4bbe7237f33f6781060485455606e2c6cfb2f19533003cc509fdafb98fa1e1dcf26f0938f8a2452ffc9d82c4647d5434934ac479dfd13bb75",
   "6cc6f52999e9a72fabf20fe69c91fd1c376c2c4db3a3b0027091e52ca213e066f12a9cd163eca066dc2a8514ea57e6be294cbb94dc28b418bae3ec8aedde9cea",
   "837cd8174d1d816c974a3408ab3e2a32257dc3e3e7277d7c1d4d4d688824a806",
   "0c3bae35575497fbdd686d1744864277534b3bedd82f197813387db9aa2b3d50",
   "e6eab92f8463b17761ad2043b938ee08103f04ec5acb95ce5260ec5574b05d03",
   "d07a43b0ad67a7718798fa18d49eecc3caf17db28690facc5b58425c9bbc0322",
   "8ee940361b6f97ead15ccfaedd50ea8811cc9d352113e0f3e3cf374adbd0ac56",
   "4bfa1e31e5f3c75f22eda127f7707e644908e595eb68336c75e29e6483571408",
   "67246e00660def056361502d05aec82cd47cea3c2f2b06011bd4c73638eab10e",
   "309b769ba892860eb6b1ded564c5d4fa1b53d7aeac06448c4d538c8b36c1438ffae13f9c6a321e35a50634149c513a0324440a3da3bd2f0248b6ef24b3a59b80"},
  {"e95d63371b1bf655923611",
   "b47c5ef0a974e1ff19c9b442f577c916242338e352099b5e57c76c913e5f5a79c2c8957af7f2fe8fe43cc5d6339dd89c6e5358642f7d7603037f4e64328018ed",
   "7ab6285b28fc18377d5653e86b1020aa8d24ed2d5b8e8ff72b2a218e16ed1dc847c0c5203c38ca957ec741a7d888d490b0920abbe924742d967e96bcaccf602a",
   "a8926cf18e057105c1d7a3a32dac939c345926e532d100bf0012b3c1103480e18545a2e7d1a45182777300349932df53be2e20c3aa1689c13454f89ac89c806e",
   "638b23990c56185bb3b3ca938748c255fc7e899179668dba0133dd8e29b94f03",
   "4237077f6fef24a8d45cce4da7a6c13bb77d7255b7a860ea5ab5a444cc617834",
   "419c8c4cc3f117ed2271a78a505e67e6f78acc6538bdf846da9e7e20a94d8c0f",
   "5240a17f819e11b931a5ab0c4981a12ab1c098f073ed10330d317bf79c3d9a6b",
   "acd1442420aa571eb28402ba88a8c6efccc209d52cac895a69cacc7b158f7b4e",
   "1396c4bd44ac36a4f18788c967c597eb70edc5db4860d10586e007896fc5b000",
   "7da19b4144e86ca50357c7eb545da46a32df4cbd78362b2030d106ffd40fd708",
   "8ac2e19fafc6cf9338dcffeca0e8374f88efe80190a72a41b0871281abecf59592042bb42e9d1c1d4b5a81545a751f7bfe1cf77a460c37b442fb00d6e8facd5d"},
  {"0ebac0ab70a6fb",
   "4c20cb257769efd309c3741b11aee8b7dfcf04a12a799783422a48e1fab2d90db7ac830ae7628c2a7faffe97d8c605f7c7294b31a27e4f17a6f4d7f70dc87aa2",
   "0d2b173a7fcd6c5041d2e318d872900f01abced531efc820fa4e6a74ec8dd38be9bbb21784b5cc87509183c0a358913dd122a66424577800775a602dade30234",
   "74b824fe1e8d088b0284b01725bc1c5ad531aaebcc91bfe7c79565d60cf82baf1d608d1395d65860c54ba8baccf21ad7da4ef0bf0a33653524d59cd5dc1711bf",
   "c7dcb67073e6b86eb5c9a47c331710fc38cd09d78a9b099b0365ea5cdd1f6501",
   "265cd46828c32f78aafbb94b2756e9b8e98dcfba409cde6e4302845c757a7f5f",
   "d0ca773e453d27dd42a04ea5e32dd26585d9f7329e8363ebe46c0d6a7331550e",
   "7cde97f642d6fce3506cb8834868cf098467304f83e4a3571a56510607dfb25a",
   "289805312744ba70efdf55e56ace647442cc977d9bb3ebabcf8ea807502e473a",
   "35c6e61a9e625b18d8de0a87bc33f747723edc889d406c45cbb47e2754d73b0d",
   "699a5880e8d9af29c3ef4333c94cf69a7b2835e7a0bb77cdcf659ae78a2cb20f",
   "938688e1aed3dab5ebeb4911044978d0b0955c8bd63049f75b3bdbfa05513af1f1e339a9424f996a83ad4d5e11cc2d7586bfccae8802a7db2e53594c8a90cc43"},
  {"50ee06e81fe1f37e558c30027c",
   "84db66ae8cd847175a3e2dc5ed21a67c92688d9fae5d7799e0a74735f72417859fbc137c1531f05c2c4e5c907e1f4b48311eaa6d914832a415a41051b8fcc95e",
   "5fa11a05d76a6d1044b240610b6ac6b48f31772c163ca3de96912d949d442477602fbd65e8db551f99051fa2ae9547f1237a0fbded896ba12c37b8bdefb43c44",
   "c7dffbc0dc82d58f24cbb07178f3fa0609b06c8b2e5b2e219065418f66ca5f641b18a15bab94b8cbf95e5b7b043030e81bdf6d341d20c2c68f78d6aacb9492bd",
   "b8d8421d00d396f3cefe8b56b7cde77f2757040d0b4f821ddd99d164116f9503",
   "208cc4bb3cb0abab247746cd8f8f723ac93680a0a36b15708913ae906134bb48",
   "c88780519bdc63de10866dc04f4ea11a24e0bc31d61625f7d002e3600203ee0d",
   "28c23cf7d1ee9ab6c09eeba167e40b90b0c7f8d1c8dcc2cb7d0279eb610d1c4a",
   "2486acacc48d8bf73400645a1421d9c0db1e986dbe1d7d385b15debb26f2d72e",
   "f1519114ce001264abbb573a446757e2bb6422088d668e2ab35b1f530e252808",
   "fa25dfeb9d9501a0775e3664b5e6d2cc93add3cbc807ebdc3bff737388e0c509",
   "5e658d46b63cec701edc1ee3419d81855014d629c9a0df6239100fe04bb83e364442b51e270b1ac88cdb73a688b08543c9b6de912ed15bd64f373c37e63b2955"},
  {"c68dee1110f8cfa5662d79",
   "059b106278a2b668d482be5b163179a0e7208426db852c85dca193e9a7c3563bdbc15dcf26b9395722be66c31ee4754a4d37e0486f29bc58c5e56959ebde938e",
   "dfffe66272357ad08c4cbb2692c1938f36715bc161511a8e3566c3b870d53dd6fa6ba38f84d3f2f52d8878c0a5c9f7a1f326756f7b7946eef6d5e03d7e8a65a3",
   "aedf03899ade6082e62506d8b487ab233e81d5e98c22f61b4be4780808cefbcf88d39a750af854c78bba8ea87e1f86927e33dcfafe9623eba543f909508eb8c5",
   "0968e4c118b01bef4864bc13777b11c2c744811ed58a6bdf454c722673687f0c",
   "72b600d9d32956ea055da92f862a8cf4e573764b839f2ff964977bb98576e302",
   "e73759d522790863ce2a585cf958bb41815806909ad3fe067fa2e41a313fbd0b",
   "8eac37386f8681ee60d5ee04d93f6ddc741a6bffd1a226d2bfac1d9899bbd36c",
   "380b8756ac6ba9d331e4f1570145fb67357bcd59ee2116a6142416aaf6ebe876",
   "e16829035f374918858d91787c2d89b4ef5bbb641d57b92340b36ed4a7a3340c",
   "95db161d372df013efc43a4a9c1f11b93b78a74570b6a03ffb694568d5e0de0b",
   "a3d7ae35d2ea8ed422e0e1ec652f5fc4820a6a796abd4b2b5f0d08fe8df78a71805f4af9b8a7e792ed641dbb7f85ac6c09d7291cf4058f46287ad8e8230ac50a"},
};

// rust/sdk/src/types.rs, test_secret_encryption: key [8; 32], "artemis"
static const uint8_t g_secret[145] = {
  1, 134, 178, 251, 18, 193, 244, 162, 122, 194, 0, 239, 255, 128, 253, 39, 199, 249, 145, 226, 252,
  83, 165, 81, 50, 46, 17, 1, 94, 108, 224, 139, 51, 137, 152, 176, 230, 203, 184, 172, 75, 181, 206,
  151, 188, 22, 100, 113, 224, 151, 68, 63, 202, 164, 225, 84, 155, 141, 169, 49, 255, 75, 1, 95, 250,
  34, 92, 203, 156, 129, 84, 16, 20, 149, 49, 86, 63, 245, 116, 36, 82, 116, 215, 136, 197, 154, 126,
  99, 99, 127, 79, 29, 23, 74, 172, 149, 20, 2, 43, 102, 29, 82, 89, 102, 225, 83, 64, 229, 247, 232,
  194, 207, 6, 129, 183, 46, 4, 52, 205, 109, 240, 64, 67, 15, 226, 185, 186, 54, 162, 20, 219, 250,
  162, 103, 164, 76, 121, 87, 140, 147, 118, 109, 107, 35, 7,
};


static void
test_blake2s(void) {
  uint8_t out[32], key[32], msg[200];
  xcrypt_blake2s_t s;

  for(int i=0; i<200; i++) {
    msg[i] = (uint8_t)i;
  }
  for(int i=0; i<32; i++) {
    key[i] = (uint8_t)i;
  }
  CHECK(!xcrypt_blake2s(out, 32, NULL, 0, "abc", 3) && same_hex(out, 32, B2S_ABC));
  CHECK(!xcrypt_blake2s(out, 32, key, 32, "", 0) && same_hex(out, 32, B2S_EMPTY_KEYED));
  CHECK(!xcrypt_blake2s(out, 32, NULL, 0, msg, 64) && same_hex(out, 32, B2S_64));
  CHECK(!xcrypt_blake2s(out, 32, NULL, 0, msg, 65) && same_hex(out, 32, B2S_65));
  for(int i=0; i<32; i++) {
    key[i] = (uint8_t)(7 + i);
  }
  // in pieces of every size, over the block's edges
  xcrypt_blake2s_init(&s, 16, key, 32);
  for(int at=0, step=1; at < 200; at += step, step++) {
    xcrypt_blake2s_update(&s, msg + at, (size_t)(at + step > 200 ? 200 - at : step));
  }
  xcrypt_blake2s_final(&s, out);
  CHECK(same_hex(out, 16, B2S_LONG_KEYED16));
  CHECK(xcrypt_blake2s(out, 33, NULL, 0, "", 0) == -1 && xcrypt_blake2s(out, 32, key, 33, "", 0) == -1);
}


static void
test_noise(void) {
  uint8_t rs_priv[32], rs[32], e_pub[32], ct[64], out[64], re[32], ct2[64];
  xcrypt_noise_t n;
  size_t len;

  unhex(NOISE_RS_SEED, rs_priv, 32);
  // the realm's public key, from its private one (X25519 of the base point)
  {
    extern int crypto_scalarmult_curve25519_base(unsigned char *q, const unsigned char *n);
    crypto_scalarmult_curve25519_base(rs, rs_priv);
  }
  queue_hex(NOISE_E_SEED); // the ephemeral key xcrypt_noise_start makes
  CHECK(!xcrypt_noise_start(&n, rs, "\"Recover1\"", 10, e_pub, ct));
  CHECK(same_hex(e_pub, 32, NOISE_E_PUB));
  CHECK(same_hex(ct, 26, NOISE_CT1));
  unhex(NOISE_RE_PUB, re, 32);
  len = unhex(NOISE_CT2, ct2, sizeof ct2);
  CHECK(!xcrypt_noise_finish(&n, re, ct2, len, out) && !memcmp(out, "the realm's answer", 18));
  CHECK(!xcrypt_noise_seal(&n, "first", 5, ct) && same_hex(ct, 21, NOISE_T1));
  len = unhex(NOISE_T2, ct2, sizeof ct2);
  CHECK(!xcrypt_noise_open(&n, ct2, len, out) && !memcmp(out, "reply", 5));
  CHECK(!xcrypt_noise_seal(&n, "second", 6, ct) && same_hex(ct, 22, NOISE_T3));
  // a message changed, or seen twice, does not open
  CHECK(xcrypt_noise_open(&n, ct2, len, out) == -1);
  ct2[0] ^= 1;
  n.recv_n = 0;
  CHECK(xcrypt_noise_open(&n, ct2, len, out) == -1);
}


static void
test_oprf(void) {
  for(size_t i=0; i < sizeof g_oprf / sizeof g_oprf[0]; i++) {
    const oprf_vector_t *v = &g_oprf[i];
    uint8_t input[64], seed[64], beta[32], pub[32], r[32], u[32], w[32], beta_t[32], c[32], beta_z[32];
    uint8_t out[64];
    size_t len = unhex(v->input, input, sizeof input);
    extern int crypto_scalarmult_ristretto255_base(unsigned char *q, const unsigned char *n);
    extern int crypto_scalarmult_ristretto255(unsigned char *q, const unsigned char *n, const unsigned char *p);

    unhex(v->private_seed, seed, 64);
    xcrypt_scalar_wide(beta, seed);
    CHECK(same_hex(beta, 32, v->private_key));
    CHECK(!crypto_scalarmult_ristretto255_base(pub, beta) && same_hex(pub, 32, v->public_key));
    unhex(v->blinding_seed, seed, 64);
    xcrypt_scalar_wide(r, seed);
    CHECK(same_hex(r, 32, v->blinding));
    CHECK(!xcrypt_oprf_blind(input, len, r, u) && same_hex(u, 32, v->blinded_input));
    CHECK(!crypto_scalarmult_ristretto255(w, beta, u) && same_hex(w, 32, v->blinded_output));
    // the realm's proof is the vector's; ours checks it, and not a changed one
    unhex(v->beta_t_seed, seed, 64);
    xcrypt_scalar_wide(beta_t, seed);
    unhex(v->proof_c, c, 32);
    unhex(v->proof_beta_z, beta_z, 32);
    CHECK(!xcrypt_oprf_check(u, pub, w, c, beta_z));
    c[0] ^= 1;
    CHECK(xcrypt_oprf_check(u, pub, w, c, beta_z) == -1);
    c[0] ^= 1;
    CHECK(xcrypt_oprf_check(u, pub, u, c, beta_z) == -1);
    CHECK(!xcrypt_oprf_finish(input, len, r, w, out) && same_hex(out, 64, v->output));
  }
}


static void
test_juicebox(void) {
  uint8_t version[16], access[32], seed[32], key[32], out[128];
  xcrypt_cbor_out_t o;
  uint8_t buf[128], c[32], beta_z[32];

  memset(version, 5, sizeof version);
  CHECK(!xcrypt_pin_hash("1234", 4, version, "artemis", 7, access, seed));
  CHECK(same_hex(access, 32, "2935da84c97423b37f3457231b877ce6ac2093501d7255dbee1deb09a5d8821b"));
  CHECK(same_hex(seed, 32, "87c8c9b5d3ea9fea83b6ac6a64e25b97c4722ca4e40bea2523efea262125e22a"));

  memset(key, 8, sizeof key);
  CHECK(xcrypt_secret_open_key(key, g_secret, out) == 7 && !memcmp(out, "artemis", 7));
  key[0] = 9;
  CHECK(xcrypt_secret_open_key(key, g_secret, out) == -1);

  // a proof of c = -2, beta_z = -1, as the SDK writes it
  unhex("ebd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010", c, 32);
  unhex("ecd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010", beta_z, 32);
  memset(&o, 0, sizeof o);
  o.buf = buf;
  o.size = sizeof buf;
  xcrypt_cbor_map(&o, 2);
  xcrypt_cbor_text(&o, "c");
  xcrypt_cbor_bytes(&o, c, 32);
  xcrypt_cbor_text(&o, "beta_z");
  xcrypt_cbor_bytes(&o, beta_z, 32);
  CHECK(!o.full && same_hex(buf, o.len, "a261635820ebd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010"
                                        "66626574615f7a5820ecd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010"));
}


static void
test_shares(void) {
  // f(x) = s + a x + b x^2, three of five shares
  uint8_t s[32], a[32], b[32], share[5][32], x[32], term[32], out[32], pick[3][32];
  uint8_t point[5][32], want[32];
  uint32_t index[5] = {1, 2, 3, 4, 5}, some[3] = {2, 4, 5};
  extern void crypto_core_ristretto255_scalar_mul(unsigned char *z, const unsigned char *x, const unsigned char *y);
  extern void crypto_core_ristretto255_scalar_add(unsigned char *z, const unsigned char *x, const unsigned char *y);
  extern int crypto_scalarmult_ristretto255_base(unsigned char *q, const unsigned char *n);

  xcrypt_scalar_random(s);
  xcrypt_scalar_random(a);
  xcrypt_scalar_random(b);
  for(int i=0; i<5; i++) {
    memset(x, 0, sizeof x);
    x[0] = (uint8_t)index[i];
    crypto_core_ristretto255_scalar_mul(term, b, x);    // b x
    crypto_core_ristretto255_scalar_add(term, term, a); // a + b x
    crypto_core_ristretto255_scalar_mul(term, term, x); // a x + b x^2
    crypto_core_ristretto255_scalar_add(share[i], term, s);
    crypto_scalarmult_ristretto255_base(point[i], share[i]);
  }
  CHECK(!xcrypt_combine_scalars(index, (const uint8_t (*)[32])share, 5, out) && !memcmp(out, s, 32));
  for(int i=0; i<3; i++) {
    memcpy(pick[i], share[some[i] - 1], 32);
  }
  CHECK(!xcrypt_combine_scalars(some, (const uint8_t (*)[32])pick, 3, out) && !memcmp(out, s, 32));
  CHECK(!xcrypt_combine_scalars(some, (const uint8_t (*)[32])pick, 2, out) && memcmp(out, s, 32));
  crypto_scalarmult_ristretto255_base(want, s);
  for(int i=0; i<3; i++) {
    memcpy(pick[i], point[some[i] - 1], 32);
  }
  CHECK(!xcrypt_combine_points(some, (const uint8_t (*)[32])pick, 3, out) && !memcmp(out, want, 32));
  index[1] = 1;
  CHECK(xcrypt_combine_scalars(index, (const uint8_t (*)[32])share, 2, out) == -1);
}


static void
test_xchat(void) {
  uint8_t identity[32], pub[65], wrapped[128], key[32], msg_key[32], sig[64], msg[256], box[64], text[32];
  size_t wlen, mlen;

  unhex(ECIES_IDENTITY, identity, 32);
  CHECK(!xcrypt_p256_public(identity, pub) && same_hex(pub, 65, ECIES_IDENTITY_PUB));
  wlen = unhex(ECIES_WRAPPED, wrapped, sizeof wrapped);
  CHECK(wlen == 113 && !xcrypt_key_open(identity, wrapped, wlen, key) && same_hex(key, 32, ECIES_KEY));
  wrapped[100] ^= 1;
  CHECK(xcrypt_key_open(identity, wrapped, wlen, key) == -1);

  unhex(ECIES_KEY, key, 32);
  CHECK(!xcrypt_message_key(key, 0, "1", "2", msg_key) && !memcmp(msg_key, key, 32));
  CHECK(!xcrypt_message_key(key, 1, "1234567890", "a1b2c3d4-0000-4000-8000-123456789abc", msg_key));
  CHECK(same_hex(msg_key, 32, MESSAGE_KEY_V1));
  CHECK(xcrypt_message_key(key, 2, "1", "2", msg_key) == -1);
  CHECK(!xcrypt_message_seal(msg_key, "hello there", 11, box));
  CHECK(!xcrypt_message_open(msg_key, box, 11 + 40, text) && !memcmp(text, "hello there", 11));
  box[30] ^= 1;
  CHECK(xcrypt_message_open(msg_key, box, 11 + 40, text) == -1);

  mlen = unhex(ECDSA_MSG, msg, sizeof msg);
  unhex(ECDSA_SIG, sig, 64);
  CHECK(!xcrypt_p256_verify(pub, msg, mlen, sig));
  msg[0] ^= 1;
  CHECK(xcrypt_p256_verify(pub, msg, mlen, sig) == -1);
  msg[0] ^= 1;
  CHECK(!xcrypt_p256_sign(identity, msg, mlen, sig) && !xcrypt_p256_verify(pub, msg, mlen, sig));
}


/**
 * a message's signature: the bytes it covers at each version, as X's SDK
 * makes them (crates/core/src/core/signature_version_tests.rs), its own
 * signature checked with its key, and the keys X lists
 **/
static void
test_signed(void) {
  static const char prefix[] = "MessageCreateEvent,11111111-2222-3333-4444-555555555555,1491585161162473473,"
                               "2:1491585161162473473";
  uint8_t spki[91], point[65], sig[64], priv[32], other[65];
  char buf[512], want[512];
  xcrypt_signed_t m = {"11111111-2222-3333-4444-555555555555", "1491585161162473473", "2:1491585161162473473",
                       "5", (const uint8_t *)"Hello", 5, 1, 1, 10};

  // the SDK's vector, and the same without the encryption version
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) == (long)strlen(SDK_SIGNED) && !strcmp(buf, SDK_SIGNED));
  CHECK(unhex(SDK_SPKI, spki, sizeof spki) == 91 && !xcrypt_p256_key(spki, 91, point));
  CHECK(point[0] == 4 && !memcmp(point, spki + 26, 65));
  unhex(SDK_SIG, sig, 64);
  CHECK(!xcrypt_signed_verify(point, &m, sig));
  m.has_encryption = 0;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, SDK_SIGNED_NULL));
  CHECK(xcrypt_signed_verify(point, &m, sig) == -1);
  m.has_encryption = 1;
  sig[10] ^= 1;
  CHECK(xcrypt_signed_verify(point, &m, sig) == -1);
  sig[10] ^= 1;
  m.contents = (const uint8_t *)"Hellp";
  CHECK(xcrypt_signed_verify(point, &m, sig) == -1);
  m.contents = (const uint8_t *)"Hello";
  m.message_id = "11111111-2222-3333-4444-555555555556";
  CHECK(xcrypt_signed_verify(point, &m, sig) == -1);
  m.message_id = "11111111-2222-3333-4444-555555555555";
  CHECK(!xcrypt_signed_verify(point, &m, sig));

  // the versions: 8 without the suffix, 9 with it, 10 and 11 with the
  // encryption version too
  m.version = 8;
  m.key_version = "8";
  snprintf(want, sizeof want, "%s,8,SGVsbG8", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
  m.version = 9;
  m.key_version = "5";
  snprintf(want, sizeof want, "%s,5,SGVsbG8,signature_version=9", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
  m.version = 11;
  snprintf(want, sizeof want, "%s,5,SGVsbG8,1,signature_version=11", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));

  // the key version as a number, or null
  m.version = 7;
  m.key_version = "005";
  snprintf(want, sizeof want, "%s,5,SGVsbG8", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
  m.key_version = "+12";
  snprintf(want, sizeof want, "%s,12,SGVsbG8", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
  m.key_version = "-0";
  snprintf(want, sizeof want, "%s,0,SGVsbG8", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
  m.key_version = "-9223372036854775808";
  snprintf(want, sizeof want, "%s,-9223372036854775808,SGVsbG8", prefix);
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
  snprintf(want, sizeof want, "%s,null,SGVsbG8", prefix);
  {
    const char *nulls[] = {"v1", "", "-", "1 ", "9223372036854775808", NULL};
    for(int i=0; i < 6; i++) {
      m.key_version = nulls[i];
      CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(buf, want));
    }
  }

  // the contents' base64, unpadded; no conversation, none of it
  m.key_version = "1";
  m.conversation = NULL;
  m.contents = (const uint8_t *)"\xfb\xff";
  m.len = 2;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 &&
        !strcmp(buf, "MessageCreateEvent,11111111-2222-3333-4444-555555555555,1491585161162473473,1,+/8"));
  m.len = 1;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(strrchr(buf, ','), ",+w"));
  m.contents = NULL;
  m.len = 0;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !strcmp(strrchr(buf, ','), ","));
  {
    static uint8_t big[3000];
    static char out[4200];
    for(size_t i=0; i < sizeof big; i++) {
      big[i] = (uint8_t)(i * 7);
    }
    m.contents = big;
    m.len = sizeof big;
    CHECK(xcrypt_signed_bytes(&m, out, sizeof out) == (long)strlen(out) && strlen(strrchr(out, ',') + 1) == 4000);
    // what does not fit is counted, and cut
    CHECK(xcrypt_signed_bytes(&m, buf, 10) == (long)strlen(out) && strlen(buf) == 9 && !memcmp(buf, out, 9));
  }

  // none for the SDK: versions out of range, ids with commas or missing
  m.contents = (const uint8_t *)"Hello";
  m.len = 5;
  m.conversation = "2:1491585161162473473";
  m.version = 1;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) == -1 && !buf[0]);
  m.version = 12;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) == -1);
  m.version = 10;
  m.conversation = "2,3";
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) == -1);
  CHECK(xcrypt_signed_verify(point, &m, sig) == -1);
  m.conversation = "2:1491585161162473473";
  m.sender = "";
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) == -1);
  m.sender = NULL;
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) == -1);

  // signature versions as the events write them
  CHECK(xcrypt_signed_version("10") == 10 && xcrypt_signed_version("2") == 2 && xcrypt_signed_version("011") == 11);
  CHECK(!xcrypt_signed_version("1") && !xcrypt_signed_version("12") && !xcrypt_signed_version("") &&
        !xcrypt_signed_version(NULL) && !xcrypt_signed_version("10a") && !xcrypt_signed_version("99999999999"));

  // a key: the point itself, its SPKI, nothing else
  CHECK(!xcrypt_p256_key(point, 65, other) && !memcmp(other, point, 65));
  CHECK(xcrypt_p256_key(point, 64, other) == -1 && xcrypt_p256_key(spki, 90, other) == -1);
  spki[5] ^= 1;
  CHECK(xcrypt_p256_key(spki, 91, other) == -1);
  spki[5] ^= 1;
  point[0] = 2;
  CHECK(xcrypt_p256_key(point, 65, other) == -1);

  // signed with a key of ours, checked with it and not with another's
  m.sender = "1491585161162473473";
  unhex(ECIES_IDENTITY, priv, 32);
  CHECK(!xcrypt_p256_public(priv, point));
  CHECK(xcrypt_signed_bytes(&m, buf, sizeof buf) > 0 && !xcrypt_p256_sign(priv, buf, strlen(buf), sig));
  CHECK(!xcrypt_signed_verify(point, &m, sig));
  unhex(SDK_SPKI, spki, sizeof spki);
  xcrypt_p256_key(spki, 91, other);
  CHECK(xcrypt_signed_verify(other, &m, sig) == -1);
  // a point off the curve is refused
  point[40] ^= 1;
  CHECK(xcrypt_signed_verify(point, &m, sig) == -1);
}


/**
 * an attachment's bytes, as X Chat stores them: the secretstream of
 * XChaCha20-Poly1305, in 1024-byte chunks, opened by xcrypt_media_open
 **/
static void
test_media(void) {
  static uint8_t plain[3000], cipher[4096], out[4096];
  uint8_t key[32], att1[32], att1b[32], att2[32];
  crypto_secretstream_xchacha20poly1305_state st;
  unsigned char header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
  size_t clen, off, olen;

  // the attachment key: HKDF, so it is the same for the same id and differs
  unhex(ECIES_KEY, key, 32);
  CHECK(!xcrypt_attachment_key(key, "att-1", att1) && !xcrypt_attachment_key(key, "att-1", att1b));
  CHECK(!memcmp(att1, att1b, 32));
  CHECK(!xcrypt_attachment_key(key, "att-2", att2) && memcmp(att1, att2, 32));
  CHECK(xcrypt_attachment_key(key, NULL, att2) == -1);

  // a file of a few chunks, encrypted the way X Chat does, opens back whole
  for(size_t i=0; i < sizeof plain; i++) {
    plain[i] = (uint8_t)(i * 7 + 1);
  }
  crypto_secretstream_xchacha20poly1305_init_push(&st, header, att1);
  memcpy(cipher, header, sizeof header);
  clen = sizeof header;
  for(off=0; off < sizeof plain; ) {
    size_t n = sizeof plain - off < 1024 ? sizeof plain - off : 1024;
    int last = off + n >= sizeof plain;
    unsigned long long cl = 0;
    crypto_secretstream_xchacha20poly1305_push(&st, cipher + clen, &cl, plain + off, n, NULL, 0,
      last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
    clen += (size_t)cl;
    off += n;
  }
  CHECK(!xcrypt_media_open(att1, cipher, clen, out, sizeof out, &olen));
  CHECK(olen == sizeof plain && !memcmp(out, plain, sizeof plain));

  // the wrong key, a buffer too small, and a corrupted byte all fail
  CHECK(xcrypt_media_open(att2, cipher, clen, out, sizeof out, &olen) == -1);
  CHECK(xcrypt_media_open(att1, cipher, clen, out, 100, &olen) == -1);
  cipher[40] ^= 1;
  CHECK(xcrypt_media_open(att1, cipher, clen, out, sizeof out, &olen) == -1);
}


static void
test_cbor(void) {
  uint8_t buf[256], ct[40];
  const uint8_t *bytes;
  xcrypt_cbor_out_t o = {buf, 0, sizeof buf, 0};
  xcrypt_cbor_t root, ok, data, hs;
  uint64_t n;
  size_t got;

  memset(ct, 0xab, sizeof ct);
  // {"Ok": {"Handshake": {"handshake": {...}, "session_lifetime": {"secs": 600, "nanos": 0}}}}
  xcrypt_cbor_map(&o, 1);
  xcrypt_cbor_text(&o, "Ok");
  xcrypt_cbor_map(&o, 1);
  xcrypt_cbor_text(&o, "Handshake");
  xcrypt_cbor_map(&o, 2);
  xcrypt_cbor_text(&o, "handshake");
  xcrypt_cbor_map(&o, 2);
  xcrypt_cbor_text(&o, "server_ephemeral_public");
  xcrypt_cbor_bytes(&o, ct, 32);
  xcrypt_cbor_text(&o, "payload_ciphertext");
  xcrypt_cbor_bytes(&o, ct, sizeof ct);
  xcrypt_cbor_text(&o, "session_lifetime");
  xcrypt_cbor_map(&o, 2);
  xcrypt_cbor_text(&o, "secs");
  xcrypt_cbor_uint(&o, 600);
  xcrypt_cbor_text(&o, "nanos");
  xcrypt_cbor_uint(&o, 0);
  CHECK(!o.full);
  root.p = buf;
  root.len = o.len;
  CHECK(xcrypt_cbor_size(root) == o.len);
  CHECK(xcrypt_cbor_is(root, "Ok", &ok) && !xcrypt_cbor_is(root, "InvalidAuth", NULL));
  CHECK(xcrypt_cbor_is(ok, "Handshake", &data) && !xcrypt_cbor_is(ok, "Transport", NULL));
  CHECK(!xcrypt_cbor_get(data, "handshake", &hs));
  CHECK(!xcrypt_cbor_get_bytes(hs, "server_ephemeral_public", &bytes, 32, NULL) && bytes[0] == 0xab);
  CHECK(!xcrypt_cbor_get_bytes(hs, "payload_ciphertext", &bytes, 0, &got) && got == sizeof ct);
  CHECK(xcrypt_cbor_get_bytes(hs, "payload_ciphertext", &bytes, 16, &got) == -1);
  CHECK(!xcrypt_cbor_get(data, "session_lifetime", &hs) && !xcrypt_cbor_get_uint(hs, "secs", &n) && n == 600);
  // an answer of a variant without data, and one cut short
  root.p = (const uint8_t *)"\x6eMissingSession";
  root.len = 15;
  CHECK(xcrypt_cbor_is(root, "MissingSession", NULL) && !xcrypt_cbor_is(root, "Missing", NULL));
  root.p = buf;
  root.len = o.len - 1;
  CHECK(!xcrypt_cbor_size(root) && !xcrypt_cbor_is(root, "Ok", &ok));
  // the full message does not fit a small buffer
  o.len = 0;
  o.size = 8;
  xcrypt_cbor_text(&o, "a text too long");
  CHECK(o.full);
}


int
main(void) {
  xcrypt_init(test_random);
  test_blake2s();
  test_noise();
  test_oprf();
  test_juicebox();
  test_shares();
  test_xchat();
  test_signed();
  test_media();
  test_cbor();
  if(g_failed) {
    printf("%d crypto checks failed\n", g_failed);
    return 1;
  }
  printf("all crypto checks passed\n");
  return 0;
}
