#include <stdlib.h>
#include <string.h>

#include "dtls_srtp.h"
#include "edgertc_stats.h"
#include "ports.h"
#include "sctp.h"
#include "utils.h"
#if CONFIG_USE_USRSCTP
#include <usrsctp.h>
#endif

#if CONFIG_EDGERTC_SCTP_DEBUG
#define SCTP_DIAG_LOG(fmt, ...) LOGI("sctp_diag: " fmt, ##__VA_ARGS__)
#else
#define SCTP_DIAG_LOG(fmt, ...)
#endif

static const uint32_t crc32c_table[4][256] = {
  {
    0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
    0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
    0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
    0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
    0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
    0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
    0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
    0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
    0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
    0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
    0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
    0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
    0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
    0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
    0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
    0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
    0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
    0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
    0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
    0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
    0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
    0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
    0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
    0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
    0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
    0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
    0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
    0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
    0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
    0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
    0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
    0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
    0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
    0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
    0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
    0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
    0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
    0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
    0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
    0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
    0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
    0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
    0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
    0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
    0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
    0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
    0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
    0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
    0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
    0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
    0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
    0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
    0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
    0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
    0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
    0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
    0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
    0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
    0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
    0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
    0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
    0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
    0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
    0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L},
    {
        0x00000000L, 0x13A29877L, 0x274530EEL, 0x34E7A899L,
        0x4E8A61DCL, 0x5D28F9ABL, 0x69CF5132L, 0x7A6DC945L,
        0x9D14C3B8L, 0x8EB65BCFL, 0xBA51F356L, 0xA9F36B21L,
        0xD39EA264L, 0xC03C3A13L, 0xF4DB928AL, 0xE7790AFDL,
        0x3FC5F181L, 0x2C6769F6L, 0x1880C16FL, 0x0B225918L,
        0x714F905DL, 0x62ED082AL, 0x560AA0B3L, 0x45A838C4L,
        0xA2D13239L, 0xB173AA4EL, 0x859402D7L, 0x96369AA0L,
        0xEC5B53E5L, 0xFFF9CB92L, 0xCB1E630BL, 0xD8BCFB7CL,
        0x7F8BE302L, 0x6C297B75L, 0x58CED3ECL, 0x4B6C4B9BL,
        0x310182DEL, 0x22A31AA9L, 0x1644B230L, 0x05E62A47L,
        0xE29F20BAL, 0xF13DB8CDL, 0xC5DA1054L, 0xD6788823L,
        0xAC154166L, 0xBFB7D911L, 0x8B507188L, 0x98F2E9FFL,
        0x404E1283L, 0x53EC8AF4L, 0x670B226DL, 0x74A9BA1AL,
        0x0EC4735FL, 0x1D66EB28L, 0x298143B1L, 0x3A23DBC6L,
        0xDD5AD13BL, 0xCEF8494CL, 0xFA1FE1D5L, 0xE9BD79A2L,
        0x93D0B0E7L, 0x80722890L, 0xB4958009L, 0xA737187EL,
        0xFF17C604L, 0xECB55E73L, 0xD852F6EAL, 0xCBF06E9DL,
        0xB19DA7D8L, 0xA23F3FAFL, 0x96D89736L, 0x857A0F41L,
        0x620305BCL, 0x71A19DCBL, 0x45463552L, 0x56E4AD25L,
        0x2C896460L, 0x3F2BFC17L, 0x0BCC548EL, 0x186ECCF9L,
        0xC0D23785L, 0xD370AFF2L, 0xE797076BL, 0xF4359F1CL,
        0x8E585659L, 0x9DFACE2EL, 0xA91D66B7L, 0xBABFFEC0L,
        0x5DC6F43DL, 0x4E646C4AL, 0x7A83C4D3L, 0x69215CA4L,
        0x134C95E1L, 0x00EE0D96L, 0x3409A50FL, 0x27AB3D78L,
        0x809C2506L, 0x933EBD71L, 0xA7D915E8L, 0xB47B8D9FL,
        0xCE1644DAL, 0xDDB4DCADL, 0xE9537434L, 0xFAF1EC43L,
        0x1D88E6BEL, 0x0E2A7EC9L, 0x3ACDD650L, 0x296F4E27L,
        0x53028762L, 0x40A01F15L, 0x7447B78CL, 0x67E52FFBL,
        0xBF59D487L, 0xACFB4CF0L, 0x981CE469L, 0x8BBE7C1EL,
        0xF1D3B55BL, 0xE2712D2CL, 0xD69685B5L, 0xC5341DC2L,
        0x224D173FL, 0x31EF8F48L, 0x050827D1L, 0x16AABFA6L,
        0x6CC776E3L, 0x7F65EE94L, 0x4B82460DL, 0x5820DE7AL,
        0xFBC3FAF9L, 0xE861628EL, 0xDC86CA17L, 0xCF245260L,
        0xB5499B25L, 0xA6EB0352L, 0x920CABCBL, 0x81AE33BCL,
        0x66D73941L, 0x7575A136L, 0x419209AFL, 0x523091D8L,
        0x285D589DL, 0x3BFFC0EAL, 0x0F186873L, 0x1CBAF004L,
        0xC4060B78L, 0xD7A4930FL, 0xE3433B96L, 0xF0E1A3E1L,
        0x8A8C6AA4L, 0x992EF2D3L, 0xADC95A4AL, 0xBE6BC23DL,
        0x5912C8C0L, 0x4AB050B7L, 0x7E57F82EL, 0x6DF56059L,
        0x1798A91CL, 0x043A316BL, 0x30DD99F2L, 0x237F0185L,
        0x844819FBL, 0x97EA818CL, 0xA30D2915L, 0xB0AFB162L,
        0xCAC27827L, 0xD960E050L, 0xED8748C9L, 0xFE25D0BEL,
        0x195CDA43L, 0x0AFE4234L, 0x3E19EAADL, 0x2DBB72DAL,
        0x57D6BB9FL, 0x447423E8L, 0x70938B71L, 0x63311306L,
        0xBB8DE87AL, 0xA82F700DL, 0x9CC8D894L, 0x8F6A40E3L,
        0xF50789A6L, 0xE6A511D1L, 0xD242B948L, 0xC1E0213FL,
        0x26992BC2L, 0x353BB3B5L, 0x01DC1B2CL, 0x127E835BL,
        0x68134A1EL, 0x7BB1D269L, 0x4F567AF0L, 0x5CF4E287L,
        0x04D43CFDL, 0x1776A48AL, 0x23910C13L, 0x30339464L,
        0x4A5E5D21L, 0x59FCC556L, 0x6D1B6DCFL, 0x7EB9F5B8L,
        0x99C0FF45L, 0x8A626732L, 0xBE85CFABL, 0xAD2757DCL,
        0xD74A9E99L, 0xC4E806EEL, 0xF00FAE77L, 0xE3AD3600L,
        0x3B11CD7CL, 0x28B3550BL, 0x1C54FD92L, 0x0FF665E5L,
        0x759BACA0L, 0x663934D7L, 0x52DE9C4EL, 0x417C0439L,
        0xA6050EC4L, 0xB5A796B3L, 0x81403E2AL, 0x92E2A65DL,
        0xE88F6F18L, 0xFB2DF76FL, 0xCFCA5FF6L, 0xDC68C781L,
        0x7B5FDFFFL, 0x68FD4788L, 0x5C1AEF11L, 0x4FB87766L,
        0x35D5BE23L, 0x26772654L, 0x12908ECDL, 0x013216BAL,
        0xE64B1C47L, 0xF5E98430L, 0xC10E2CA9L, 0xD2ACB4DEL,
        0xA8C17D9BL, 0xBB63E5ECL, 0x8F844D75L, 0x9C26D502L,
        0x449A2E7EL, 0x5738B609L, 0x63DF1E90L, 0x707D86E7L,
        0x0A104FA2L, 0x19B2D7D5L, 0x2D557F4CL, 0x3EF7E73BL,
        0xD98EEDC6L, 0xCA2C75B1L, 0xFECBDD28L, 0xED69455FL,
        0x97048C1AL, 0x84A6146DL, 0xB041BCF4L, 0xA3E32483L
    },
    {
        0x00000000L, 0xA541927EL, 0x4F6F520DL, 0xEA2EC073L,
        0x9EDEA41AL, 0x3B9F3664L, 0xD1B1F617L, 0x74F06469L,
        0x38513EC5L, 0x9D10ACBBL, 0x773E6CC8L, 0xD27FFEB6L,
        0xA68F9ADFL, 0x03CE08A1L, 0xE9E0C8D2L, 0x4CA15AACL,
        0x70A27D8AL, 0xD5E3EFF4L, 0x3FCD2F87L, 0x9A8CBDF9L,
        0xEE7CD990L, 0x4B3D4BEEL, 0xA1138B9DL, 0x045219E3L,
        0x48F3434FL, 0xEDB2D131L, 0x079C1142L, 0xA2DD833CL,
        0xD62DE755L, 0x736C752BL, 0x9942B558L, 0x3C032726L,
        0xE144FB14L, 0x4405696AL, 0xAE2BA919L, 0x0B6A3B67L,
        0x7F9A5F0EL, 0xDADBCD70L, 0x30F50D03L, 0x95B49F7DL,
        0xD915C5D1L, 0x7C5457AFL, 0x967A97DCL, 0x333B05A2L,
        0x47CB61CBL, 0xE28AF3B5L, 0x08A433C6L, 0xADE5A1B8L,
        0x91E6869EL, 0x34A714E0L, 0xDE89D493L, 0x7BC846EDL,
        0x0F382284L, 0xAA79B0FAL, 0x40577089L, 0xE516E2F7L,
        0xA9B7B85BL, 0x0CF62A25L, 0xE6D8EA56L, 0x43997828L,
        0x37691C41L, 0x92288E3FL, 0x78064E4CL, 0xDD47DC32L,
        0xC76580D9L, 0x622412A7L, 0x880AD2D4L, 0x2D4B40AAL,
        0x59BB24C3L, 0xFCFAB6BDL, 0x16D476CEL, 0xB395E4B0L,
        0xFF34BE1CL, 0x5A752C62L, 0xB05BEC11L, 0x151A7E6FL,
        0x61EA1A06L, 0xC4AB8878L, 0x2E85480BL, 0x8BC4DA75L,
        0xB7C7FD53L, 0x12866F2DL, 0xF8A8AF5EL, 0x5DE93D20L,
        0x29195949L, 0x8C58CB37L, 0x66760B44L, 0xC337993AL,
        0x8F96C396L, 0x2AD751E8L, 0xC0F9919BL, 0x65B803E5L,
        0x1148678CL, 0xB409F5F2L, 0x5E273581L, 0xFB66A7FFL,
        0x26217BCDL, 0x8360E9B3L, 0x694E29C0L, 0xCC0FBBBEL,
        0xB8FFDFD7L, 0x1DBE4DA9L, 0xF7908DDAL, 0x52D11FA4L,
        0x1E704508L, 0xBB31D776L, 0x511F1705L, 0xF45E857BL,
        0x80AEE112L, 0x25EF736CL, 0xCFC1B31FL, 0x6A802161L,
        0x56830647L, 0xF3C29439L, 0x19EC544AL, 0xBCADC634L,
        0xC85DA25DL, 0x6D1C3023L, 0x8732F050L, 0x2273622EL,
        0x6ED23882L, 0xCB93AAFCL, 0x21BD6A8FL, 0x84FCF8F1L,
        0xF00C9C98L, 0x554D0EE6L, 0xBF63CE95L, 0x1A225CEBL,
        0x8B277743L, 0x2E66E53DL, 0xC448254EL, 0x6109B730L,
        0x15F9D359L, 0xB0B84127L, 0x5A968154L, 0xFFD7132AL,
        0xB3764986L, 0x1637DBF8L, 0xFC191B8BL, 0x595889F5L,
        0x2DA8ED9CL, 0x88E97FE2L, 0x62C7BF91L, 0xC7862DEFL,
        0xFB850AC9L, 0x5EC498B7L, 0xB4EA58C4L, 0x11ABCABAL,
        0x655BAED3L, 0xC01A3CADL, 0x2A34FCDEL, 0x8F756EA0L,
        0xC3D4340CL, 0x6695A672L, 0x8CBB6601L, 0x29FAF47FL,
        0x5D0A9016L, 0xF84B0268L, 0x1265C21BL, 0xB7245065L,
        0x6A638C57L, 0xCF221E29L, 0x250CDE5AL, 0x804D4C24L,
        0xF4BD284DL, 0x51FCBA33L, 0xBBD27A40L, 0x1E93E83EL,
        0x5232B292L, 0xF77320ECL, 0x1D5DE09FL, 0xB81C72E1L,
        0xCCEC1688L, 0x69AD84F6L, 0x83834485L, 0x26C2D6FBL,
        0x1AC1F1DDL, 0xBF8063A3L, 0x55AEA3D0L, 0xF0EF31AEL,
        0x841F55C7L, 0x215EC7B9L, 0xCB7007CAL, 0x6E3195B4L,
        0x2290CF18L, 0x87D15D66L, 0x6DFF9D15L, 0xC8BE0F6BL,
        0xBC4E6B02L, 0x190FF97CL, 0xF321390FL, 0x5660AB71L,
        0x4C42F79AL, 0xE90365E4L, 0x032DA597L, 0xA66C37E9L,
        0xD29C5380L, 0x77DDC1FEL, 0x9DF3018DL, 0x38B293F3L,
        0x7413C95FL, 0xD1525B21L, 0x3B7C9B52L, 0x9E3D092CL,
        0xEACD6D45L, 0x4F8CFF3BL, 0xA5A23F48L, 0x00E3AD36L,
        0x3CE08A10L, 0x99A1186EL, 0x738FD81DL, 0xD6CE4A63L,
        0xA23E2E0AL, 0x077FBC74L, 0xED517C07L, 0x4810EE79L,
        0x04B1B4D5L, 0xA1F026ABL, 0x4BDEE6D8L, 0xEE9F74A6L,
        0x9A6F10CFL, 0x3F2E82B1L, 0xD50042C2L, 0x7041D0BCL,
        0xAD060C8EL, 0x08479EF0L, 0xE2695E83L, 0x4728CCFDL,
        0x33D8A894L, 0x96993AEAL, 0x7CB7FA99L, 0xD9F668E7L,
        0x9557324BL, 0x3016A035L, 0xDA386046L, 0x7F79F238L,
        0x0B899651L, 0xAEC8042FL, 0x44E6C45CL, 0xE1A75622L,
        0xDDA47104L, 0x78E5E37AL, 0x92CB2309L, 0x378AB177L,
        0x437AD51EL, 0xE63B4760L, 0x0C158713L, 0xA954156DL,
        0xE5F54FC1L, 0x40B4DDBFL, 0xAA9A1DCCL, 0x0FDB8FB2L,
        0x7B2BEBDBL, 0xDE6A79A5L, 0x3444B9D6L, 0x91052BA8L
    },
    {
        0x00000000L, 0xDD45AAB8L, 0xBF672381L, 0x62228939L,
        0x7B2231F3L, 0xA6679B4BL, 0xC4451272L, 0x1900B8CAL,
        0xF64463E6L, 0x2B01C95EL, 0x49234067L, 0x9466EADFL,
        0x8D665215L, 0x5023F8ADL, 0x32017194L, 0xEF44DB2CL,
        0xE964B13DL, 0x34211B85L, 0x560392BCL, 0x8B463804L,
        0x924680CEL, 0x4F032A76L, 0x2D21A34FL, 0xF06409F7L,
        0x1F20D2DBL, 0xC2657863L, 0xA047F15AL, 0x7D025BE2L,
        0x6402E328L, 0xB9474990L, 0xDB65C0A9L, 0x06206A11L,
        0xD725148BL, 0x0A60BE33L, 0x6842370AL, 0xB5079DB2L,
        0xAC072578L, 0x71428FC0L, 0x136006F9L, 0xCE25AC41L,
        0x2161776DL, 0xFC24DDD5L, 0x9E0654ECL, 0x4343FE54L,
        0x5A43469EL, 0x8706EC26L, 0xE524651FL, 0x3861CFA7L,
        0x3E41A5B6L, 0xE3040F0EL, 0x81268637L, 0x5C632C8FL,
        0x45639445L, 0x98263EFDL, 0xFA04B7C4L, 0x27411D7CL,
        0xC805C650L, 0x15406CE8L, 0x7762E5D1L, 0xAA274F69L,
        0xB327F7A3L, 0x6E625D1BL, 0x0C40D422L, 0xD1057E9AL,
        0xABA65FE7L, 0x76E3F55FL, 0x14C17C66L, 0xC984D6DEL,
        0xD0846E14L, 0x0DC1C4ACL, 0x6FE34D95L, 0xB2A6E72DL,
        0x5DE23C01L, 0x80A796B9L, 0xE2851F80L, 0x3FC0B538L,
        0x26C00DF2L, 0xFB85A74AL, 0x99A72E73L, 0x44E284CBL,
        0x42C2EEDAL, 0x9F874462L, 0xFDA5CD5BL, 0x20E067E3L,
        0x39E0DF29L, 0xE4A57591L, 0x8687FCA8L, 0x5BC25610L,
        0xB4868D3CL, 0x69C32784L, 0x0BE1AEBDL, 0xD6A40405L,
        0xCFA4BCCFL, 0x12E11677L, 0x70C39F4EL, 0xAD8635F6L,
        0x7C834B6CL, 0xA1C6E1D4L, 0xC3E468EDL, 0x1EA1C255L,
        0x07A17A9FL, 0xDAE4D027L, 0xB8C6591EL, 0x6583F3A6L,
        0x8AC7288AL, 0x57828232L, 0x35A00B0BL, 0xE8E5A1B3L,
        0xF1E51979L, 0x2CA0B3C1L, 0x4E823AF8L, 0x93C79040L,
        0x95E7FA51L, 0x48A250E9L, 0x2A80D9D0L, 0xF7C57368L,
        0xEEC5CBA2L, 0x3380611AL, 0x51A2E823L, 0x8CE7429BL,
        0x63A399B7L, 0xBEE6330FL, 0xDCC4BA36L, 0x0181108EL,
        0x1881A844L, 0xC5C402FCL, 0xA7E68BC5L, 0x7AA3217DL,
        0x52A0C93FL, 0x8FE56387L, 0xEDC7EABEL, 0x30824006L,
        0x2982F8CCL, 0xF4C75274L, 0x96E5DB4DL, 0x4BA071F5L,
        0xA4E4AAD9L, 0x79A10061L, 0x1B838958L, 0xC6C623E0L,
        0xDFC69B2AL, 0x02833192L, 0x60A1B8ABL, 0xBDE41213L,
        0xBBC47802L, 0x6681D2BAL, 0x04A35B83L, 0xD9E6F13BL,
        0xC0E649F1L, 0x1DA3E349L, 0x7F816A70L, 0xA2C4C0C8L,
        0x4D801BE4L, 0x90C5B15CL, 0xF2E73865L, 0x2FA292DDL,
        0x36A22A17L, 0xEBE780AFL, 0x89C50996L, 0x5480A32EL,
        0x8585DDB4L, 0x58C0770CL, 0x3AE2FE35L, 0xE7A7548DL,
        0xFEA7EC47L, 0x23E246FFL, 0x41C0CFC6L, 0x9C85657EL,
        0x73C1BE52L, 0xAE8414EAL, 0xCCA69DD3L, 0x11E3376BL,
        0x08E38FA1L, 0xD5A62519L, 0xB784AC20L, 0x6AC10698L,
        0x6CE16C89L, 0xB1A4C631L, 0xD3864F08L, 0x0EC3E5B0L,
        0x17C35D7AL, 0xCA86F7C2L, 0xA8A47EFBL, 0x75E1D443L,
        0x9AA50F6FL, 0x47E0A5D7L, 0x25C22CEEL, 0xF8878656L,
        0xE1873E9CL, 0x3CC29424L, 0x5EE01D1DL, 0x83A5B7A5L,
        0xF90696D8L, 0x24433C60L, 0x4661B559L, 0x9B241FE1L,
        0x8224A72BL, 0x5F610D93L, 0x3D4384AAL, 0xE0062E12L,
        0x0F42F53EL, 0xD2075F86L, 0xB025D6BFL, 0x6D607C07L,
        0x7460C4CDL, 0xA9256E75L, 0xCB07E74CL, 0x16424DF4L,
        0x106227E5L, 0xCD278D5DL, 0xAF050464L, 0x7240AEDCL,
        0x6B401616L, 0xB605BCAEL, 0xD4273597L, 0x09629F2FL,
        0xE6264403L, 0x3B63EEBBL, 0x59416782L, 0x8404CD3AL,
        0x9D0475F0L, 0x4041DF48L, 0x22635671L, 0xFF26FCC9L,
        0x2E238253L, 0xF36628EBL, 0x9144A1D2L, 0x4C010B6AL,
        0x5501B3A0L, 0x88441918L, 0xEA669021L, 0x37233A99L,
        0xD867E1B5L, 0x05224B0DL, 0x6700C234L, 0xBA45688CL,
        0xA345D046L, 0x7E007AFEL, 0x1C22F3C7L, 0xC167597FL,
        0xC747336EL, 0x1A0299D6L, 0x782010EFL, 0xA565BA57L,
        0xBC65029DL, 0x6120A825L, 0x0302211CL, 0xDE478BA4L,
        0x31035088L, 0xEC46FA30L, 0x8E647309L, 0x5321D9B1L,
        0x4A21617BL, 0x9764CBC3L, 0xF54642FAL, 0x2803E842L
    }
};

uint32_t crc32c(uint32_t crc, const uint8_t* data, unsigned int length) {
  while (length && ((uintptr_t)data & (sizeof(uint32_t) - 1))) {
    crc = crc32c_table[0][(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    length--;
  }

  while (length >= 4) {
    uint32_t word = *(const uint32_t*)data;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    word = __builtin_bswap32(word);
#endif
    crc ^= word;
    crc = crc32c_table[3][crc & 0xFF] ^
          crc32c_table[2][(crc >> 8) & 0xFF] ^
          crc32c_table[1][(crc >> 16) & 0xFF] ^
          crc32c_table[0][crc >> 24];
    data += 4;
    length -= 4;
  }

  while (length--) {
    crc = crc32c_table[0][(crc ^ *data++) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xffffffff;
}

static uint32_t sctp_get_checksum(Sctp* sctp, const uint8_t* buf, size_t len) {
  return crc32c(0xffffffff, buf, len);
}

#if !CONFIG_USE_USRSCTP
#define SCTP_FORWARD_RETRY_MS 100U
#define SCTP_GAP_WAIT_MS 100U
#define SCTP_RETRANSMIT_CHECK_MS 150U
#define SCTP_RETRANSMIT_INTERVAL_MS 300U

struct SctpSentPacket {
  SctpSentPacket* next;
  size_t length;
  uint32_t tsn;
  uint32_t last_send_ms;
  uint32_t resend_count;
  uint8_t data[];
};

enum {
  SCTP_FORWARD_STATE_IDLE,
  SCTP_FORWARD_STATE_GAP_WAIT,
  SCTP_FORWARD_STATE_PENDING,
};

static int sctp_tsn_gte(uint32_t lhs, uint32_t rhs) {
  return (int32_t)(lhs - rhs) >= 0;
}

static SctpChunkParam* sctp_find_init_param(SctpInitChunk* init,
                                            uint16_t type) {
  uint16_t chunk_length = ntohs(init->common.length);
  size_t pos = sizeof(SctpInitChunk);

  while (pos + sizeof(SctpChunkParam) <= chunk_length) {
    SctpChunkParam* param = (SctpChunkParam*)((uint8_t*)init + pos);
    uint16_t param_length = ntohs(param->length);

    if (param_length < sizeof(SctpChunkParam) ||
        pos + param_length > chunk_length) {
      return NULL;
    }
    if (ntohs(param->type) == type) {
      return param;
    }
    pos += (param_length + 3U) & ~3U;
  }
  return NULL;
}

#endif

static int sctp_outgoing_data_cb(void* userdata, void* buf, size_t len, uint8_t tos, uint8_t set_df) {
  Sctp* sctp = (Sctp*)userdata;
  int ret = dtls_srtp_write(sctp->dtls_srtp, buf, len);

#if CONFIG_EDGERTC_SCTP_DEBUG && !CONFIG_USE_USRSCTP
  sctp->last_dtls_write_ret = ret;
#endif
  return ret < 0 ? -1 : 0;
}

#if !CONFIG_USE_USRSCTP
static int sctp_stream_no_retransmit(Sctp* sctp, uint16_t sid) {
  int i;

  for (i = 0; i < sctp->stream_count; i++) {
    if (sctp->stream_table[i].sid == sid) {
      return sctp->stream_table[i].no_retransmit;
    }
  }
  return 0;
}

static int sctp_sent_cache_has_space(Sctp* sctp, size_t len,
                                     size_t payload_max) {
  size_t required = (len - 1U) / payload_max + 1U;

  if (required > sctp->sent_packet_limit ||
      sctp->sent_packet_count > sctp->sent_packet_limit - required) {
    LOGW("SCTP send cache full used=%u required=%u limit=%u packets",
         (unsigned int)sctp->sent_packet_count, (unsigned int)required,
         (unsigned int)sctp->sent_packet_limit);
    return 0;
  }
  return 1;
}

static SctpSentPacket* sctp_cache_sent_packet(Sctp* sctp, uint32_t tsn,
                                              const uint8_t* data,
                                              size_t length,
                                              uint32_t now_ms) {
  SctpSentPacket* packet = malloc(sizeof(*packet) + length);

  if (packet == NULL) {
    LOGE("SCTP send cache allocation failed length=%u",
         (unsigned int)length);
    return NULL;
  }
  packet->next = NULL;
  packet->length = length;
  packet->tsn = tsn;
  packet->last_send_ms = now_ms;
  packet->resend_count = 0;
  memcpy(packet->data, data, length);
  if (sctp->sent_packets_tail != NULL) {
    sctp->sent_packets_tail->next = packet;
  } else {
    sctp->sent_packets = packet;
  }
  sctp->sent_packets_tail = packet;
  sctp->sent_packet_count++;
  return packet;
}

static int sctp_retransmit_packet(Sctp* sctp, SctpSentPacket* packet,
                                  uint32_t now_ms, const char* reason) {
  if (now_ms - packet->last_send_ms <= SCTP_RETRANSMIT_INTERVAL_MS) {
    return 0;
  }
  if (sctp_outgoing_data_cb(sctp, packet->data, packet->length, 0, 0) < 0) {
    LOGW("SCTP retransmit failed tsn=%lu reason=%s",
         (unsigned long)packet->tsn, reason);
    return -1;
  }
  packet->last_send_ms = now_ms;
  packet->resend_count++;
  LOGW("SCTP retransmit tsn=%lu retry=%u reason=%s",
       (unsigned long)packet->tsn, packet->resend_count, reason);
  return 1;
}

static int sctp_read_gap_block(SctpSackChunk* sack, uint16_t index,
                               uint16_t* start, uint16_t* end) {
  uint16_t value;

  memcpy(&value, sack->blocks + (size_t)index * 4U, sizeof(value));
  *start = ntohs(value);
  memcpy(&value, sack->blocks + (size_t)index * 4U + sizeof(value),
         sizeof(value));
  *end = ntohs(value);
  return *start > 0 && *start <= *end;
}

static int sctp_tsn_is_gap_acked(SctpSackChunk* sack, uint16_t gap_blocks,
                                 uint32_t cumulative_tsn_ack, uint32_t tsn) {
  uint32_t offset = tsn - cumulative_tsn_ack;
  uint16_t i;

  if (offset == 0 || offset > UINT16_MAX) {
    return 0;
  }
  for (i = 0; i < gap_blocks; i++) {
    uint16_t start;
    uint16_t end;

    sctp_read_gap_block(sack, i, &start, &end);
    if (offset >= start && offset <= end) {
      return 1;
    }
  }
  return 0;
}

static void sctp_handle_reliable_sack(Sctp* sctp, SctpSackChunk* sack,
                                      uint16_t gap_blocks,
                                      uint32_t cumulative_tsn_ack,
                                      uint16_t highest_gap,
                                      uint32_t now_ms) {
  SctpSentPacket* current = sctp->sent_packets;
  SctpSentPacket* previous = NULL;
  uint32_t highest_sent_tsn = sctp->tsn - 1;

  if (!sctp_tsn_gte(cumulative_tsn_ack, sctp->last_sack_tsn) ||
      !sctp_tsn_gte(highest_sent_tsn, cumulative_tsn_ack) ||
      (highest_gap > 0 &&
       !sctp_tsn_gte(highest_sent_tsn,
                     cumulative_tsn_ack + highest_gap))) {
    return;
  }
  sctp->last_sack_tsn = cumulative_tsn_ack;

  while (current != NULL) {
    SctpSentPacket* next = current->next;

    if (sctp_tsn_gte(cumulative_tsn_ack, current->tsn) ||
        sctp_tsn_is_gap_acked(sack, gap_blocks,
                             cumulative_tsn_ack, current->tsn)) {
      if (previous != NULL) {
        previous->next = next;
      } else {
        sctp->sent_packets = next;
      }
      if (sctp->sent_packets_tail == current) {
        sctp->sent_packets_tail = previous;
      }
      sctp->sent_packet_count--;
      if (current->resend_count > 0) {
        LOGI("SCTP retransmission acknowledged tsn=%lu retries=%u",
             (unsigned long)current->tsn, current->resend_count);
      }
      free(current);
      current = next;
      continue;
    }
    if (highest_gap > 0 &&
        sctp_tsn_gte(current->tsn, cumulative_tsn_ack + 1) &&
        sctp_tsn_gte(cumulative_tsn_ack + highest_gap, current->tsn)) {
      sctp_retransmit_packet(sctp, current, now_ms, "SACK gap");
    }
    previous = current;
    current = next;
  }
}

static int sctp_send_forward_tsn(Sctp* sctp, uint32_t now_ms, int force) {
  SctpPacket* packet;
  SctpForwardTsnChunk* chunk;
  size_t packet_length = sizeof(SctpHeader) + sizeof(SctpForwardTsnChunk);
  int ret;

  if (sctp->forward_state != SCTP_FORWARD_STATE_PENDING ||
      !sctp->peer_supports_forward_tsn) {
    return 0;
  }
  if (!force &&
      now_ms - sctp->forward_state_since_ms < SCTP_FORWARD_RETRY_MS) {
    return 0;
  }

  packet = (SctpPacket*)sctp->buf;
  chunk = (SctpForwardTsnChunk*)packet->chunks;
  memset(packet, 0, packet_length);
  packet->header.source_port = htons(sctp->local_port);
  packet->header.destination_port = htons(sctp->remote_port);
  packet->header.verification_tag = sctp->verification_tag;
  chunk->common.type = SCTP_FORWARD_TSN;
  chunk->common.length = htons(sizeof(SctpForwardTsnChunk));
  chunk->new_cumulative_tsn = htonl(sctp->last_pr_tsn);
  packet->header.checksum = sctp_get_checksum(
      sctp, (const uint8_t*)packet, packet_length);
  sctp->forward_state_since_ms = now_ms;
  sctp->forward_attempts++;
  ret = sctp_outgoing_data_cb(sctp, packet, packet_length, 0, 0);
  if (sctp->forward_attempts == 1 || sctp->forward_attempts == 30) {
    SCTP_DIAG_LOG("forward send tsn=%lu attempt=%lu ret=%d dtls_ret=%d",
                  (unsigned long)sctp->last_pr_tsn,
                  (unsigned long)sctp->forward_attempts, ret,
                  sctp->last_dtls_write_ret);
  }
  if (sctp->forward_attempts == 1 ||
      (sctp->forward_attempts % 10U) == 0) {
    LOGD("SCTP forward tsn=%lu attempt=%lu ret=%d",
         (unsigned long)sctp->last_pr_tsn,
         (unsigned long)sctp->forward_attempts, ret);
  }
  return ret;
}

static void sctp_start_forward_tsn(Sctp* sctp, uint32_t now_ms,
                                   const char* reason) {
  if (!sctp->peer_supports_forward_tsn) {
    return;
  }
  sctp->forward_state = SCTP_FORWARD_STATE_PENDING;
  sctp->forward_state_since_ms = 0;
  sctp->forward_attempts = 0;
  SCTP_DIAG_LOG("pending start reason=%s tx_tsn=%lu ack_tsn=%lu last_pr_tsn=%lu dtls_ret=%d",
                reason, (unsigned long)(sctp->tsn - 1),
                (unsigned long)sctp->last_sack_tsn,
                (unsigned long)sctp->last_pr_tsn,
                sctp->last_dtls_write_ret);
  LOGD("SCTP forward start reason=%s last_pr_tsn=%lu",
       reason, (unsigned long)sctp->last_pr_tsn);
  sctp_send_forward_tsn(sctp, now_ms, 1);
}

static void sctp_handle_sack(Sctp* sctp, SctpSackChunk* sack,
                             size_t available_length) {
  uint16_t sack_length = ntohs(sack->common.length);
  uint16_t gap_blocks;
  uint16_t duplicate_tsns;
  uint32_t cumulative_tsn_ack;
  uint32_t now_ms;
  uint32_t retransmit_now_ms;
  uint16_t highest_gap = 0;
  uint16_t previous_gap_end = 0;
  uint16_t i;
  size_t blocks_length;

  if (sack_length < sizeof(SctpSackChunk) ||
      sack_length > available_length) {
    return;
  }
  gap_blocks = ntohs(sack->number_of_gap_ack_blocks);
  duplicate_tsns = ntohs(sack->number_of_dup_tsns);
  blocks_length = (size_t)gap_blocks * 4U +
                  (size_t)duplicate_tsns * sizeof(uint32_t);
  if (blocks_length > sack_length - sizeof(SctpSackChunk)) {
    return;
  }
  for (i = 0; i < gap_blocks; i++) {
    uint16_t start;
    uint16_t end;

    if (!sctp_read_gap_block(sack, i, &start, &end) ||
        start <= previous_gap_end) {
      return;
    }
    previous_gap_end = end;
    highest_gap = end;
  }

  cumulative_tsn_ack = ntohl(sack->cumulative_tsn_ack);
  now_ms = ports_get_epoch_time();
  retransmit_now_ms = ports_get_time_ms();
#if CONFIG_EDGERTC_SCTP_DEBUG
  sctp->debug_last_sack_ms = now_ms;
  sctp->debug_peer_rwnd = ntohl(sack->a_rwnd);
#endif
  sctp->sack_count++;
  if ((sctp->sack_count % 500U) == 0) {
    LOGD("SCTP progress tx_tsn=%lu ack_tsn=%lu gaps=%u forward=%u",
         (unsigned long)(sctp->tsn - 1),
         (unsigned long)cumulative_tsn_ack,
         (unsigned int)gap_blocks,
         (unsigned int)sctp->forward_state);
  }
  sctp_handle_reliable_sack(sctp, sack, gap_blocks,
                            cumulative_tsn_ack, highest_gap,
                            retransmit_now_ms);

  if (sctp->forward_state == SCTP_FORWARD_STATE_PENDING) {
    if (sctp_tsn_gte(cumulative_tsn_ack, sctp->last_pr_tsn)) {
      SCTP_DIAG_LOG("pending cleared ack_tsn=%lu last_pr_tsn=%lu attempts=%lu",
                    (unsigned long)cumulative_tsn_ack,
                    (unsigned long)sctp->last_pr_tsn,
                    (unsigned long)sctp->forward_attempts);
      LOGD("SCTP forward acknowledged ack_tsn=%lu attempts=%lu",
           (unsigned long)cumulative_tsn_ack,
           (unsigned long)sctp->forward_attempts);
      sctp->forward_state = SCTP_FORWARD_STATE_IDLE;
      sctp->forward_attempts = 0;
    } else {
      sctp_send_forward_tsn(sctp, now_ms, 0);
    }
    return;
  }

  if (gap_blocks == 0 ||
      sctp_tsn_gte(cumulative_tsn_ack, sctp->last_pr_tsn)) {
    sctp->forward_state = SCTP_FORWARD_STATE_IDLE;
    return;
  }
  if (sctp->forward_state != SCTP_FORWARD_STATE_GAP_WAIT ||
      cumulative_tsn_ack != sctp->gap_cumulative_tsn) {
    sctp->forward_state = SCTP_FORWARD_STATE_GAP_WAIT;
    sctp->gap_cumulative_tsn = cumulative_tsn_ack;
    sctp->forward_state_since_ms = now_ms;
    return;
  }
  if (now_ms - sctp->forward_state_since_ms >= SCTP_GAP_WAIT_MS) {
    sctp_start_forward_tsn(sctp, now_ms, "sack-gap");
  }
}

static int sctp_outgoing_data_internal(Sctp* sctp, uint8_t* buf, size_t len,
                                       size_t headroom, size_t tailroom,
                                       SctpDataPpid ppid, uint16_t sid,
                                       int zero_copy) {
  const size_t header_size = sizeof(SctpPacket) + sizeof(SctpDataChunk);
  size_t payload_max;
  size_t final_payload_len;
  size_t packet_len;
  size_t payload_len;
  size_t pos = 0;
  size_t remaining = len;
  static uint16_t sqn = 0;
  uint16_t message_sqn;
  int send_ret;
  int no_retransmit = ppid != PPID_CONTROL &&
                      sctp_stream_no_retransmit(sctp, sid);

  if (no_retransmit &&
      sctp->forward_state == SCTP_FORWARD_STATE_PENDING) {
    sctp_send_forward_tsn(sctp, ports_get_epoch_time(), 0);
    return -1;
  }

  if (SCTP_MTU <= header_size || (SCTP_MTU & 3) != 0) {
    return -1;
  }
  if (len == 0) {
    return 0;
  }
  if (buf == NULL) {
    return -1;
  }
  payload_max = SCTP_MTU - header_size;
  if (!no_retransmit && sctp->sent_packet_limit > 0 &&
      !sctp_sent_cache_has_space(sctp, len, payload_max)) {
    return -1;
  }
  if (zero_copy) {
    if (headroom < header_size ||
        (uintptr_t)buf < header_size ||
        (((uintptr_t)buf - header_size) & (sizeof(uint32_t) - 1)) != 0) {
      return -1;
    }
    final_payload_len = len % payload_max;
    if (final_payload_len == 0) {
      final_payload_len = payload_max;
    }
    packet_len = header_size + final_payload_len;
    if (((4 - (packet_len & 3)) & 3) > tailroom) {
      return -1;
    }
  }

  message_sqn = sqn++;
  while (remaining > 0) {
    SctpPacket* packet;
    SctpDataChunk* chunk;
    SctpSentPacket* sent_packet = NULL;
    uint32_t packet_tsn;

    payload_len = remaining > payload_max ? payload_max : remaining;
    if (zero_copy) {
      packet = (SctpPacket*)(buf + pos - header_size);
    } else {
      packet = (SctpPacket*)sctp->buf;
    }
    chunk = (SctpDataChunk*)packet->chunks;

    packet->header.source_port = htons(sctp->local_port);
    packet->header.destination_port = htons(sctp->remote_port);
    packet->header.verification_tag = sctp->verification_tag;
    packet->header.checksum = 0;

    chunk->type = SCTP_DATA;
    chunk->iube = 0x04;
    if (pos == 0) {
      chunk->iube |= 0x02;
    }
    if (payload_len == remaining) {
      chunk->iube |= 0x01;
    }
    chunk->length = htons(payload_len + sizeof(SctpDataChunk));
    chunk->tsn = htonl(sctp->tsn++);
    packet_tsn = ntohl(chunk->tsn);
    if (no_retransmit) {
      sctp->last_pr_tsn = packet_tsn;
    }
    chunk->sid = htons(sid);
    chunk->sqn = htons(message_sqn);
    chunk->ppid = htonl(ppid);

    if (!zero_copy) {
      memcpy(chunk->data, buf + pos, payload_len);
    }
    packet_len = header_size + payload_len;
    if ((packet_len & 3) != 0) {
      size_t padded_len = (packet_len + 3) & ~(size_t)3;
      memset((uint8_t*)packet + packet_len, 0, padded_len - packet_len);
      packet_len = padded_len;
    }
    packet->header.checksum =
        sctp_get_checksum(sctp, (const uint8_t*)packet, packet_len);

    if (!no_retransmit && sctp->sent_packet_limit > 0) {
      sent_packet = sctp_cache_sent_packet(
          sctp, packet_tsn, (const uint8_t*)packet, packet_len,
          ports_get_time_ms());
      if (sent_packet == NULL) {
        return -1;
      }
    }
    send_ret = sctp_outgoing_data_cb(sctp, packet, packet_len, 0, 0);
    if (send_ret < 0) {
      if (no_retransmit) {
        size_t unsent_payload = remaining - payload_len;
        size_t unsent_chunks =
            (unsent_payload + payload_max - 1U) / payload_max;

        sctp->tsn += (uint32_t)unsent_chunks;
        sctp->last_pr_tsn = sctp->tsn - 1U;
        sctp_start_forward_tsn(sctp, ports_get_epoch_time(), "data-send");
      } else if (sent_packet != NULL) {
        LOGW("SCTP initial send failed tsn=%lu; queued for retransmission",
             (unsigned long)sent_packet->tsn);
      }
      return -1;
    }
    pos += payload_len;
    remaining -= payload_len;
  }
  return (int)len;
}
#endif

void sctp_process_timers(Sctp* sctp) {
#if !CONFIG_USE_USRSCTP
  SctpSentPacket* packet;
  uint32_t now_ms;
  uint32_t epoch_ms;

  if (sctp == NULL) {
    return;
  }
  epoch_ms = ports_get_epoch_time();
#if CONFIG_EDGERTC_SCTP_DEBUG
  if (epoch_ms - sctp->debug_last_status_ms >= 10000U) {
    uint32_t sack_age_ms = sctp->debug_last_sack_ms == 0 ? UINT32_MAX :
                           epoch_ms - sctp->debug_last_sack_ms;

    SCTP_DIAG_LOG("status connected=%d tx_tsn=%lu ack_tsn=%lu rwnd=%lu sacks=%lu forward=%u pr_tsn=%lu attempts=%lu dtls_ret=%d sack_age_ms=%lu",
                  sctp->connected, (unsigned long)(sctp->tsn - 1),
                  (unsigned long)sctp->last_sack_tsn,
                  (unsigned long)sctp->debug_peer_rwnd,
                  (unsigned long)sctp->sack_count,
                  (unsigned int)sctp->forward_state,
                  (unsigned long)sctp->last_pr_tsn,
                  (unsigned long)sctp->forward_attempts,
                  sctp->last_dtls_write_ret,
                  (unsigned long)sack_age_ms);
    sctp->debug_last_status_ms = epoch_ms;
  }
#endif
  if (sctp->forward_state == SCTP_FORWARD_STATE_GAP_WAIT &&
      epoch_ms - sctp->forward_state_since_ms >= SCTP_GAP_WAIT_MS) {
    sctp_start_forward_tsn(sctp, epoch_ms, "gap-timeout");
  } else if (sctp->forward_state == SCTP_FORWARD_STATE_PENDING) {
    sctp_send_forward_tsn(sctp, epoch_ms, 0);
  }
  now_ms = ports_get_time_ms();
  if (now_ms - sctp->last_retransmit_check_ms <
      SCTP_RETRANSMIT_CHECK_MS) {
    return;
  }
  sctp->last_retransmit_check_ms = now_ms;
  for (packet = sctp->sent_packets; packet != NULL; packet = packet->next) {
    sctp_retransmit_packet(sctp, packet, now_ms, "timeout");
  }
#else
  (void)sctp;
#endif
}

int sctp_outgoing_data(Sctp* sctp, char* buf, size_t len,
                       SctpDataPpid ppid, uint16_t sid,
                       SctpDataMode mode, size_t headroom, size_t tailroom) {
  int ret;

  EDGERTC_STATS_BEGIN(start);
#if CONFIG_USE_USRSCTP
  struct sctp_sendv_spa spa = {0};

  (void)mode;
  (void)headroom;
  (void)tailroom;

  spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;

  spa.sendv_sndinfo.snd_sid = sid;
  spa.sendv_sndinfo.snd_flags = SCTP_EOR;
  spa.sendv_sndinfo.snd_ppid = htonl(ppid);

  ret = usrsctp_sendv(sctp->sock, buf, len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
  if (ret < 0) {
    LOGE("sctp sendv error %d: %s", errno, strerror(errno));
  }
#else
  ret = sctp_outgoing_data_internal(sctp, (uint8_t*)buf, len,
                                    headroom, tailroom,
                                    ppid, sid,
                                    mode == SCTP_DATA_ZERO_COPY);
#endif
  EDGERTC_STATS_END(EDGERTC_STAT_SCTP_SEND, start);
  return ret;
}

void sctp_add_stream_mapping(Sctp* sctp, const char* label, uint16_t sid,
                             uint8_t no_retransmit) {
  if (sctp->stream_count < SCTP_MAX_STREAMS) {
    strncpy(sctp->stream_table[sctp->stream_count].label, label, sizeof(sctp->stream_table[sctp->stream_count].label));
    sctp->stream_table[sctp->stream_count].sid = sid;
    sctp->stream_table[sctp->stream_count].no_retransmit = no_retransmit;
    sctp->stream_count++;
  } else
    LOGE("Stream table full. Cannot add more streams.");
}

static int sctp_store_data_channel_open(Sctp* sctp, uint16_t sid,
                                        char* data, size_t length) {
  uint32_t reliability_parameter;

  if (length < 12 || data[0] != DATA_CHANNEL_OPEN)
    return -1;

  uint16_t label_length = ntohs(*(uint16_t*)(data + 8));
  uint16_t protocol_length = ntohs(*(uint16_t*)(data + 10));
  if (length < 12 + label_length + protocol_length)
    return -1;

  char label[label_length + 1];
  memcpy(label, data + 12, label_length);
  label[label_length] = '\0';
  printf("DATA_CHANNEL_OPEN: Label=%s, sid=%d\n", label, sid);
  memcpy(&reliability_parameter, data + 4, sizeof(reliability_parameter));
  sctp_add_stream_mapping(
      sctp, label, sid,
      (uint8_t)data[1] == 0x81 && ntohl(reliability_parameter) == 0);
  return 0;
}

void sctp_parse_data_channel_open(Sctp* sctp, uint16_t sid, char* data, size_t length) {
  if (sctp_store_data_channel_open(sctp, sid, data, length) == 0) {
    char ack = DATA_CHANNEL_ACK;
    sctp_outgoing_data(sctp, &ack, 1, DATA_CHANNEL_PPID_CONTROL, sid,
                       SCTP_DATA_COPY, 0, 0);
  }
}

void sctp_handle_sctp_packet(Sctp* sctp, char* buf, size_t len) {
  if (len <= 29)
    return;

  if (buf[12] != 0)  // if chunk_type is no zero, it's not data
    return;

  uint16_t sid = ntohs(*(uint16_t*)(buf + 20));
  uint32_t ppid = ntohl(*(uint32_t*)(buf + 24));

  if (ppid == DATA_CHANNEL_PPID_CONTROL)
    sctp_parse_data_channel_open(sctp, sid, buf + 28, len - 28);
}

void sctp_incoming_data(Sctp* sctp, char* buf, size_t len) {
  if (!sctp || !buf)
    return;

#if CONFIG_USE_USRSCTP
  sctp_handle_sctp_packet(sctp, buf, len);
  usrsctp_conninput(sctp, buf, len, 0);
#else
  size_t length = 0;
  size_t pos = sizeof(SctpHeader);
  SctpChunkCommon* chunk_common;
  SctpPacket* in_packet = (SctpPacket*)buf;
  SctpPacket* out_packet = (SctpPacket*)sctp->buf;

  if (len < sizeof(SctpHeader)) {
    return;
  }

  // Header
#if 0
  LOGD("source_port %d", ntohs(in_packet->header.source_port));
  LOGD("destination_port %d", ntohs(in_packet->header.destination_port));
  LOGD("verification_tag %ld", ntohl(in_packet->header.verification_tag));
  LOGD("checksum %d", ntohs(in_packet->header.checksum));
#endif
  uint32_t crc32c = in_packet->header.checksum;

  in_packet->header.checksum = 0;

  if (crc32c != sctp_get_checksum(sctp, (const uint8_t*)buf, len)) {
    LOGE("checksum error");
    return;
  }

  // prepare outgoing packet
  memset(sctp->buf, 0, sizeof(sctp->buf));
  while (pos < len) {
    size_t remaining = len - pos;
    size_t chunk_length;
    size_t padded_length;
    int stop_processing = 0;

    if (remaining < sizeof(SctpChunkCommon)) {
      return;
    }

    length = 0;
    chunk_common = (SctpChunkCommon*)(buf + pos);
    chunk_length = ntohs(chunk_common->length);
    if (chunk_length < sizeof(SctpChunkCommon) ||
        chunk_length > remaining) {
      return;
    }
    padded_length = (chunk_length + 3U) & ~(size_t)3U;
    if (padded_length > remaining) {
      return;
    }

    switch (chunk_common->type) {
      case SCTP_DATA: {
        SctpDataChunk* data_chunk = (SctpDataChunk*)(buf + pos);
        SctpSackChunk* sack_chunk = (SctpSackChunk*)out_packet->chunks;
        size_t data_length;

        if (chunk_length < sizeof(SctpDataChunk)) {
          return;
        }
        data_length = chunk_length - sizeof(SctpDataChunk);

        sack_chunk->common.type = SCTP_SACK;
        sack_chunk->common.flags = 0x00;
        sack_chunk->common.length = htons(16);
        sack_chunk->cumulative_tsn_ack = data_chunk->tsn;
        sack_chunk->a_rwnd = htonl(0x02);
        length = ntohs(sack_chunk->common.length) + sizeof(SctpHeader);

        LOGD("SCTP_DATA. ppid = %ld, data = %.2x", ntohl(data_chunk->ppid), data_length > 0 ? data_chunk->data[0] : 0);
        if (ntohl(data_chunk->ppid) == DATA_CHANNEL_PPID_CONTROL &&
            data_length > 0 && data_chunk->data[0] == DATA_CHANNEL_OPEN) {
          uint16_t sid = ntohs(data_chunk->sid);
          sctp_store_data_channel_open(
              sctp, sid, (char*)data_chunk->data,
              data_length);
          data_chunk = (SctpDataChunk*)sack_chunk->blocks;
          data_chunk->type = SCTP_DATA;
          data_chunk->iube = 0x03;
          data_chunk->tsn = htonl(sctp->tsn++);
          data_chunk->sid = htons(sid);
          data_chunk->sqn = htons(0);
          data_chunk->ppid = htonl(DATA_CHANNEL_PPID_CONTROL);
          data_chunk->length = htons(1 + sizeof(SctpDataChunk));
          data_chunk->data[0] = DATA_CHANNEL_ACK;
          length += ntohs(data_chunk->length);
        } else if (ntohl(data_chunk->ppid) == DATA_CHANNEL_PPID_DOMSTRING || ntohl(data_chunk->ppid) == DATA_CHANNEL_PPID_BINARY) {
          if (sctp->onmessage) {
            sctp->onmessage((char*)data_chunk->data, data_length,
                            sctp->userdata, ntohs(data_chunk->sid));
          }
        }
        stop_processing = 1;  // Do not handle other msg
      } break;
      case SCTP_INIT: {
        LOGD("SCTP_INIT");

        SctpInitChunk* init_chunk = (SctpInitChunk*)(buf + pos);

        if (chunk_length < sizeof(SctpInitChunk)) {
          return;
        }
        sctp->verification_tag = init_chunk->initiate_tag;
        sctp->peer_supports_forward_tsn =
            sctp_find_init_param(init_chunk,
                                 SCTP_PARAM_FORWARD_TSN_SUPPORTED) != NULL;

        SctpInitChunk* init_ack = (SctpInitChunk*)out_packet->chunks;
        init_ack->common.type = SCTP_INIT_ACK;
        init_ack->common.flags = 0x00;
        init_ack->common.length = htons(20 + 8 + 4);
        init_ack->initiate_tag = htonl(0x12345678);
        init_ack->a_rwnd = htonl(0x100000);
        init_ack->number_of_outbound_streams = 0xffff;
        init_ack->number_of_inbound_streams = 0xffff;
        init_ack->initial_tsn = htonl(sctp->tsn);

        SctpChunkParam* param = init_ack->param;

        param->type = htons(SCTP_PARAM_STATE_COOKIE);
        param->length = htons(8);
        *(uint32_t*)&param->value = htonl(0x02);

        param = (SctpChunkParam*)((uint8_t*)param + 8);
        param->type = htons(SCTP_PARAM_FORWARD_TSN_SUPPORTED);
        param->length = htons(4);
        length = ntohs(init_ack->common.length) + sizeof(SctpHeader);
      } break;
      case SCTP_INIT_ACK: {
        SctpInitChunk* init_ack = (SctpInitChunk*)(buf + pos);
        SctpCookieEchoChunk* cookie_echo = (SctpCookieEchoChunk*)out_packet->chunks;
        SctpChunkParam* param;

        if (chunk_length < sizeof(SctpInitChunk)) {
          return;
        }
        sctp->verification_tag = init_ack->initiate_tag;
        sctp->peer_supports_forward_tsn =
            sctp_find_init_param(init_ack,
                                 SCTP_PARAM_FORWARD_TSN_SUPPORTED) != NULL;
        param = sctp_find_init_param(init_ack, SCTP_PARAM_STATE_COOKIE);
        if (param == NULL) {
          break;
        }

        cookie_echo->common.type = SCTP_COOKIE_ECHO;
        cookie_echo->common.flags = 0x00;
        // cookie echo: type + flag + length (4 bytes) + cookie
        cookie_echo->common.length = htons(ntohs(param->length));
        // param: type + length (4 bytes) + cookie
        memcpy(cookie_echo->cookie, param->value, ntohs(param->length) - 4);
        length = ntohs(cookie_echo->common.length) + sizeof(SctpHeader);
      } break;
      case SCTP_SACK: {
        SctpSackChunk* sack = (SctpSackChunk*)(buf + pos);

        if (chunk_length < sizeof(SctpSackChunk)) {
          return;
        }
        sctp_handle_sack(sctp, sack, chunk_length);
#if 0
        LOGD("SCTP_SACK");
        sack = (SctpSackChunk*)in_packet->chunks;
        LOGD("cumulative_tsn_ack %d", ntohl(sack->cumulative_tsn_ack));
        LOGD("a_rwnd %d", ntohl(sack->a_rwnd));
        LOGD("number_of_gap_ack_blocks %d", sack->number_of_gap_ack_blocks);
        LOGD("number_of_dup_tsns %d", sack->number_of_dup_tsns);
#endif
// XXX: unordered sequence
#if 0
        if (sack->number_of_gap_ack_blocks > 0) {

          int blocks = ntohs(sack->number_of_gap_ack_blocks);
          LOGW("cumulative_tsn_ack: %ld, number_of_gap_ack_blocks: %d",
           ntohl(sack->cumulative_tsn_ack), blocks);

          for (int i = 0; i < blocks; i++) {

            uint16_t *start = (uint16_t*)sack->blocks + i*2;
            uint16_t *end = (uint16_t*)sack->blocks + i*2 + 1;
            LOGW("start: %d, end: %d", ntohs(*start), ntohs(*end));
            sctp->tsn = ntohl(sack->cumulative_tsn_ack) + 1;// + (*start) - 1;
          }
        } else if (sack->number_of_dup_tsns > 0) {

          LOGW("cumulative_tsn_ack: %ld, number_of_dup_tsns: %d",
           ntohl(sack->cumulative_tsn_ack),
           ntohs(sack->number_of_dup_tsns));
        }
#endif
        break;
      }
      case SCTP_COOKIE_ECHO: {
        LOGD("SCTP_COOKIE_ECHO");
        SctpChunkCommon* common = (SctpChunkCommon*)out_packet->chunks;
        common->type = SCTP_COOKIE_ACK;
        common->length = htons(4);
        length = ntohs(common->length) + sizeof(SctpHeader);
        stop_processing = 1;  // Do not handle other msg
        if (!sctp->connected) {
          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }
        }
      } break;
      case SCTP_COOKIE_ACK: {
        if (!sctp->connected) {
          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }
        }
        break;
      }
      case SCTP_HEARTBEAT: {
        SctpChunkCommon* heartbeat_ack = (SctpChunkCommon*)out_packet->chunks;

        if (chunk_length > sizeof(sctp->buf) - sizeof(SctpHeader)) {
          return;
        }

        memcpy(heartbeat_ack, chunk_common, chunk_length);
        heartbeat_ack->type = SCTP_HEARTBEAT_ACK;
        heartbeat_ack->flags = 0;
        length = sizeof(SctpHeader) + chunk_length;
      } break;
      case SCTP_HEARTBEAT_ACK:
        LOGD("SCTP_HEARTBEAT_ACK");
        break;
      case SCTP_ABORT: {
        uint16_t cause_code = 0;
        uint16_t cause_length = 0;

        if (chunk_length >= sizeof(SctpChunkCommon) + 4U) {
          memcpy(&cause_code, (uint8_t *)chunk_common + sizeof(SctpChunkCommon),
                 sizeof(cause_code));
          memcpy(&cause_length,
                 (uint8_t *)chunk_common + sizeof(SctpChunkCommon) + 2U,
                 sizeof(cause_length));
          cause_code = ntohs(cause_code);
          cause_length = ntohs(cause_length);
        }
        SCTP_DIAG_LOG("abort received connected=%d flags=%u chunk_len=%u cause=%u cause_len=%u",
                      sctp->connected, (unsigned int)chunk_common->flags,
                      (unsigned int)chunk_length, (unsigned int)cause_code,
                      (unsigned int)cause_length);
        sctp->connected = 0;
        if (sctp->onclose) {
          sctp->onclose(sctp->userdata);
        }
        break;
      }
      default:
        LOGI("Unknown chunk type %d", chunk_common->type);
        length = 0;
        break;
    }

    out_packet->header.source_port = htons(sctp->local_port);
    out_packet->header.destination_port = htons(sctp->remote_port);
    out_packet->header.verification_tag = sctp->verification_tag;
    out_packet->header.checksum = 0x00;

    if (length > 0) {
      // padding 4
      length = (4 * ((length + 3) / 4));
      if (length > sizeof(sctp->buf)) {
        return;
      }
      out_packet->header.checksum = sctp_get_checksum(sctp, sctp->buf, length);
      dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
      // sctp_outgoing_data_cb(sctp, sctp->buf, SCTP_MTU, 0, 0);
    }
    if (stop_processing) {
      break;
    }
    pos += padded_length;
  }
#endif
}

static int sctp_handle_incoming_data(Sctp* sctp, char* data, size_t len, uint32_t ppid, uint16_t sid, int flags) {
#if CONFIG_USE_USRSCTP
  switch (ppid) {
    case DATA_CHANNEL_PPID_CONTROL:
      break;

    case DATA_CHANNEL_PPID_DOMSTRING:
    case DATA_CHANNEL_PPID_BINARY:
    case DATA_CHANNEL_PPID_DOMSTRING_PARTIAL:
    case DATA_CHANNEL_PPID_BINARY_PARTIAL:

      LOGD("Got message (size = %ld)", len);
      if (sctp->onmessage) {
        sctp->onmessage(data, len, sctp->userdata, sid);
      }
      break;

    default:
      break;
  }
#endif
  return 0;
}

#if CONFIG_USE_USRSCTP

static void sctp_process_notification(Sctp* sctp, union sctp_notification* notification, size_t len) {
  if (notification->sn_header.sn_length != (uint32_t)len) {
    return;
  }

  switch (notification->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:

      switch (notification->sn_assoc_change.sac_state) {
        case SCTP_COMM_UP:

          sctp->connected = 1;
          if (sctp->onopen) {
            sctp->onopen(sctp->userdata);
          }

          break;

        case SCTP_COMM_LOST:
        case SCTP_SHUTDOWN_COMP:
          sctp->connected = 0;
          if (sctp->onclose) {
            sctp->onclose(sctp->userdata);
          }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

static int sctp_incoming_data_cb(struct socket* sock, union sctp_sockstore addr, void* data, size_t len, struct sctp_rcvinfo recv_info, int flags, void* userdata) {
  Sctp* sctp = (Sctp*)userdata;
  LOGD("Data of length %u received on stream %u with SSN %u, TSN %u, PPID %u",
       (uint32_t)len,
       recv_info.rcv_sid,
       recv_info.rcv_ssn,
       recv_info.rcv_tsn,
       ntohl(recv_info.rcv_ppid));
  if (flags & MSG_NOTIFICATION) {
    sctp_process_notification(sctp, (union sctp_notification*)data, len);
  } else {
    sctp_handle_incoming_data(sctp, data, len, ntohl(recv_info.rcv_ppid), recv_info.rcv_sid, flags);
  }
  free(data);  // we need to free the memory that usrsctp allocates
  return 0;
}
#endif

void sctp_usrsctp_init() {
#if CONFIG_USE_USRSCTP
  usrsctp_init(0, sctp_outgoing_data_cb, NULL);
#endif
}

void sctp_usrsctp_deinit() {
#if CONFIG_USE_USRSCTP
  usrsctp_finish();
#endif
}

int sctp_create_association(Sctp* sctp, DtlsSrtp* dtls_srtp) {
  sctp->dtls_srtp = dtls_srtp;
  sctp->local_port = 5000;
  sctp->remote_port = 5000;
  sctp->tsn = 1234;
#if !CONFIG_USE_USRSCTP
  sctp->last_sack_tsn = sctp->tsn - 1;
  sctp->last_retransmit_check_ms = 0;
  sctp->last_pr_tsn = sctp->tsn - 1;
  sctp->sack_count = 0;
  sctp->forward_attempts = 0;
  sctp->peer_supports_forward_tsn = 0;
  sctp->forward_state = SCTP_FORWARD_STATE_IDLE;
#endif
#if CONFIG_USE_USRSCTP
  int ret = -1;
  usrsctp_sysctl_set_sctp_ecn_enable(0);
  usrsctp_register_address(sctp);

  struct socket* sock = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP,
                                       sctp_incoming_data_cb, NULL, 0, sctp);

  if (!sock) {
    LOGE("usrsctp_socket failed");
    return -1;
  }

  do {
    if (usrsctp_set_non_blocking(sock, 1) < 0) {
      LOGE("usrsctp_set_non_blocking failed");
      break;
    }

    struct linger lopt;
    lopt.l_onoff = 1;
    lopt.l_linger = 0;
    usrsctp_setsockopt(sock, SOL_SOCKET, SO_LINGER, &lopt, sizeof(lopt));

#if 0
    struct sctp_paddrparams peer_param;
    memset(&peer_param, 0, sizeof peer_param);
    peer_param.spp_flags = SPP_PMTUD_DISABLE;
    peer_param.spp_pathmtu = 1200;
    usrsctp_setsockopt(s, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &peer_param, sizeof peer_param);
#endif

    struct sctp_assoc_value av;
    av.assoc_id = SCTP_ALL_ASSOC;
    av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av, sizeof(av));

    uint32_t nodelay = 1;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_NODELAY, &nodelay, sizeof(nodelay));

    static uint16_t event_types[] = {
        SCTP_ASSOC_CHANGE,
        SCTP_PEER_ADDR_CHANGE,
        SCTP_REMOTE_ERROR,
        SCTP_SHUTDOWN_EVENT,
        SCTP_ADAPTATION_INDICATION,
        SCTP_SEND_FAILED_EVENT,
        SCTP_SENDER_DRY_EVENT,
        SCTP_STREAM_RESET_EVENT,
        SCTP_STREAM_CHANGE_EVENT};

    struct sctp_event event;
    memset(&event, 0, sizeof(event));
    event.se_assoc_id = SCTP_ALL_ASSOC;
    event.se_on = 1;
    for (int i = 0; i < sizeof(event_types) / sizeof(uint16_t); i++) {
      event.se_type = event_types[i];
      usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
    }

    struct sctp_initmsg init_msg;
    memset(&init_msg, 0, sizeof init_msg);
    init_msg.sinit_num_ostreams = 300;
    init_msg.sinit_max_instreams = 300;
    usrsctp_setsockopt(sock, IPPROTO_SCTP, SCTP_INITMSG, &init_msg, sizeof init_msg);

    struct sockaddr_conn sconn;
    memset(&sconn, 0, sizeof(sconn));
    sconn.sconn_family = AF_CONN;
    sconn.sconn_port = htons(sctp->local_port);
    sconn.sconn_addr = (void*)sctp;
    ret = usrsctp_bind(sock, (struct sockaddr*)&sconn, sizeof(sconn));

    struct sockaddr_conn rconn;

    memset(&rconn, 0, sizeof(struct sockaddr_conn));
    rconn.sconn_family = AF_CONN;
    rconn.sconn_port = htons(sctp->remote_port);
    rconn.sconn_addr = (void*)sctp;
    ret = usrsctp_connect(sock, (struct sockaddr*)&rconn, sizeof(struct sockaddr_conn));

    if (ret < 0 && errno != EINPROGRESS) {
      LOGE("connect error");
      break;
    }

    ret = 0;

  } while (0);

  if (ret < 0) {
    sctp_destroy_association(sctp);
    return -1;
  }

  sctp->sock = sock;
#else
  // send SCTP_INIT
  int length = 0;
  SctpInitChunk* init_chunk;
  SctpChunkParam* param;
  SctpHeader* header;
  SctpPacket* out_packet = (SctpPacket*)sctp->buf;
  header = &out_packet->header;
  init_chunk = (SctpInitChunk*)out_packet->chunks;

  header->source_port = htons(sctp->local_port);
  header->destination_port = htons(sctp->remote_port);
  header->verification_tag = 0x0;
  header->checksum = 0;
  init_chunk->common.type = SCTP_INIT;
  init_chunk->common.flags = 0x00;
  init_chunk->common.length = htons(20 + 4);
  init_chunk->initiate_tag = htonl(0x12345678);
  init_chunk->a_rwnd = htonl(0x100000);
  init_chunk->number_of_outbound_streams = 0xffff;
  init_chunk->number_of_inbound_streams = 0xffff;
  init_chunk->initial_tsn = htonl(sctp->tsn);
  param = init_chunk->param;
  param->type = htons(SCTP_PARAM_FORWARD_TSN_SUPPORTED);
  param->length = htons(4);
  length = ntohs(init_chunk->common.length) + sizeof(SctpHeader);
  length = (4 * ((length + 3) / 4));
  header->checksum = sctp_get_checksum(sctp, sctp->buf, length);
  dtls_srtp_write(sctp->dtls_srtp, sctp->buf, length);
#endif

  return 0;
}

void sctp_destroy_association(Sctp* sctp) {
#if !CONFIG_USE_USRSCTP
  if (sctp != NULL) {
    while (sctp->sent_packets != NULL) {
      SctpSentPacket* packet = sctp->sent_packets;

      sctp->sent_packets = packet->next;
      free(packet);
    }
    sctp->sent_packets_tail = NULL;
    sctp->sent_packet_count = 0;
  }
#endif
#if CONFIG_USE_USRSCTP
  if (sctp && sctp->sock) {
    usrsctp_shutdown(sctp->sock, SHUT_RDWR);
    usrsctp_close(sctp->sock);
    sctp->sock = NULL;
  }
#endif
}

int sctp_is_connected(Sctp* sctp) {
  return sctp->connected;
}

void sctp_onmessage(Sctp* sctp, void (*onmessage)(char* msg, size_t len, void* userdata, uint16_t sid)) {
  sctp->onmessage = onmessage;
}

void sctp_onopen(Sctp* sctp, void (*onopen)(void* userdata)) {
  sctp->onopen = onopen;
}

void sctp_onclose(Sctp* sctp, void (*onclose)(void* userdata)) {
  sctp->onclose = onclose;
}
