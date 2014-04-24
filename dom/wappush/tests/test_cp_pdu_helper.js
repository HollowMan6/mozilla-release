/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

let CP = {};
subscriptLoader.loadSubScript("resource://gre/modules/CpPduHelper.jsm", CP);
CP.debug = do_print;

function run_test() {
  run_next_test();
}

function test_parser(rawDataArray, contentType, expectResult) {
  let data = { array: rawDataArray, offset: 0 };
  let msg = CP.PduHelper.parse(data, contentType);
  do_check_eq(msg.contentType, expectResult.contentType);
  do_check_eq(msg.content, expectResult.content);
  do_check_eq(msg.content.length, expectResult.content.length);
}

function test_hmac(rawDataArray, mac, key, expectResult) {
  let authInfo = CP.Authenticator.check(rawDataArray, 0, mac, function getNetworkPin() {
    return key;
  });
  do_check_eq(authInfo.data, rawDataArray);
  do_check_eq(authInfo.data.length, rawDataArray.length);
  do_check_eq(authInfo.checked, expectResult.checked);
  do_check_eq(authInfo.pass, expectResult.pass);
}

/*
 * Test data from OMA-TS-WAP_ProvCont-V1_1-2009 0421-C.pdf, clause 6.1
 */
let text_data_array = new Uint8Array([
  0x3C, 0x3F, 0x78, 0x6D, 0x6C, 0x20, 0x76, 0x65,
  0x72, 0x73, 0x69, 0x6F, 0x6E, 0x3D, 0x22, 0x31,
  0x2E, 0x30, 0x22, 0x3F, 0x3E, 0x3C, 0x21, 0x44,
  0x4F, 0x43, 0x54, 0x59, 0x50, 0x45, 0x20, 0x77,
  0x61, 0x70, 0x2D, 0x70, 0x72, 0x6F, 0x76, 0x69,
  0x73, 0x69, 0x6F, 0x6E, 0x69, 0x6E, 0x67, 0x64,
  0x6F, 0x63, 0x20, 0x50, 0x55, 0x42, 0x4C, 0x49,
  0x43, 0x20, 0x22, 0x2D, 0x2F, 0x2F, 0x57, 0x41,
  0x50, 0x46, 0x4F, 0x52, 0x55, 0x4D, 0x2F, 0x2F,
  0x44, 0x54, 0x44, 0x20, 0x50, 0x52, 0x4F, 0x56,
  0x20, 0x31, 0x2E, 0x30, 0x2F, 0x2F, 0x45, 0x4E,
  0x22, 0x20, 0x22, 0x68, 0x74, 0x74, 0x70, 0x3A,
  0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x77, 0x61,
  0x70, 0x66, 0x6F, 0x72, 0x75, 0x6D, 0x2E, 0x6F,
  0x72, 0x67, 0x2F, 0x44, 0x54, 0x44, 0x2F, 0x70,
  0x72, 0x6F, 0x76, 0x2E, 0x64, 0x74, 0x64, 0x22,
  0x3E, 0x3C, 0x77, 0x61, 0x70, 0x2D, 0x70, 0x72,
  0x6F, 0x76, 0x69, 0x73, 0x69, 0x6F, 0x6E, 0x69,
  0x6E, 0x67, 0x64, 0x6F, 0x63, 0x20, 0x76, 0x65,
  0x72, 0x73, 0x69, 0x6F, 0x6E, 0x3D, 0x22, 0x31,
  0x2E, 0x30, 0x22, 0x3E, 0x3C, 0x63, 0x68, 0x61,
  0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73,
  0x74, 0x69, 0x63, 0x20, 0x74, 0x79, 0x70, 0x65,
  0x3D, 0x22, 0x50, 0x58, 0x4C, 0x4F, 0x47, 0x49,
  0x43, 0x41, 0x4C, 0x22, 0x3E, 0x3C, 0x70, 0x61,
  0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D,
  0x22, 0x50, 0x52, 0x4F, 0x58, 0x59, 0x2D, 0x49,
  0x44, 0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65,
  0x3D, 0x22, 0x31, 0x37, 0x30, 0x2E, 0x31, 0x38,
  0x37, 0x2E, 0x35, 0x31, 0x2E, 0x34, 0x22, 0x2F,
  0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E,
  0x61, 0x6D, 0x65, 0x3D, 0x22, 0x4E, 0x41, 0x4D,
  0x45, 0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65,
  0x3D, 0x22, 0x42, 0x61, 0x6E, 0x6B, 0x4D, 0x61,
  0x69, 0x6E, 0x50, 0x72, 0x6F, 0x78, 0x79, 0x22,
  0x2F, 0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20,
  0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22, 0x53, 0x54,
  0x41, 0x52, 0x54, 0x50, 0x41, 0x47, 0x45, 0x22,
  0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22,
  0x68, 0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F, 0x77,
  0x77, 0x77, 0x2E, 0x62, 0x61, 0x6E, 0x6B, 0x2E,
  0x63, 0x6F, 0x6D, 0x2F, 0x73, 0x74, 0x61, 0x72,
  0x74, 0x70, 0x61, 0x67, 0x65, 0x2E, 0x77, 0x6D,
  0x6C, 0x22, 0x2F, 0x3E, 0x3C, 0x63, 0x68, 0x61,
  0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73,
  0x74, 0x69, 0x63, 0x20, 0x74, 0x79, 0x70, 0x65,
  0x3D, 0x22, 0x50, 0x58, 0x41, 0x55, 0x54, 0x48,
  0x49, 0x4E, 0x46, 0x4F, 0x22, 0x3E, 0x3C, 0x70,
  0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65,
  0x3D, 0x22, 0x50, 0x58, 0x41, 0x55, 0x54, 0x48,
  0x2D, 0x54, 0x59, 0x50, 0x45, 0x22, 0x20, 0x76,
  0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22, 0x48, 0x54,
  0x54, 0x50, 0x2D, 0x42, 0x41, 0x53, 0x49, 0x43,
  0x22, 0x2F, 0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D,
  0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22, 0x50,
  0x58, 0x41, 0x55, 0x54, 0x48, 0x2D, 0x49, 0x44,
  0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D,
  0x22, 0x70, 0x78, 0x75, 0x73, 0x65, 0x72, 0x6E,
  0x61, 0x6D, 0x65, 0x22, 0x2F, 0x3E, 0x3C, 0x70,
  0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65,
  0x3D, 0x22, 0x50, 0x58, 0x41, 0x55, 0x54, 0x48,
  0x2D, 0x50, 0x57, 0x22, 0x20, 0x76, 0x61, 0x6C,
  0x75, 0x65, 0x3D, 0x22, 0x70, 0x78, 0x75, 0x73,
  0x65, 0x72, 0x70, 0x61, 0x73, 0x73, 0x77, 0x64,
  0x22, 0x2F, 0x3E, 0x3C, 0x2F, 0x63, 0x68, 0x61,
  0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73,
  0x74, 0x69, 0x63, 0x3E, 0x3C, 0x63, 0x68, 0x61,
  0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73,
  0x74, 0x69, 0x63, 0x20, 0x74, 0x79, 0x70, 0x65,
  0x3D, 0x22, 0x50, 0x58, 0x50, 0x48, 0x59, 0x53,
  0x49, 0x43, 0x41, 0x4C, 0x22, 0x3E, 0x3C, 0x70,
  0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65,
  0x3D, 0x22, 0x50, 0x48, 0x59, 0x53, 0x49, 0x43,
  0x41, 0x4C, 0x2D, 0x50, 0x52, 0x4F, 0x58, 0x59,
  0x2D, 0x49, 0x44, 0x22, 0x20, 0x76, 0x61, 0x6C,
  0x75, 0x65, 0x3D, 0x22, 0x50, 0x52, 0x4F, 0x58,
  0x59, 0x20, 0x31, 0x22, 0x2F, 0x3E, 0x3C, 0x70,
  0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65,
  0x3D, 0x22, 0x44, 0x4F, 0x4D, 0x41, 0x49, 0x4E,
  0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D,
  0x22, 0x77, 0x77, 0x77, 0x2E, 0x62, 0x61, 0x6E,
  0x6B, 0x2E, 0x63, 0x6F, 0x6D, 0x2F, 0x22, 0x2F,
  0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E,
  0x61, 0x6D, 0x65, 0x3D, 0x22, 0x50, 0x58, 0x41,
  0x44, 0x44, 0x52, 0x22, 0x20, 0x76, 0x61, 0x6C,
  0x75, 0x65, 0x3D, 0x22, 0x31, 0x37, 0x30, 0x2E,
  0x31, 0x38, 0x37, 0x2E, 0x35, 0x31, 0x2E, 0x33,
  0x22, 0x2F, 0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D,
  0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22, 0x50,
  0x58, 0x41, 0x44, 0x44, 0x52, 0x54, 0x59, 0x50,
  0x45, 0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65,
  0x3D, 0x22, 0x49, 0x50, 0x56, 0x34, 0x22, 0x2F,
  0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E,
  0x61, 0x6D, 0x65, 0x3D, 0x22, 0x54, 0x4F, 0x2D,
  0x4E, 0x41, 0x50, 0x49, 0x44, 0x22, 0x20, 0x76,
  0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22, 0x49, 0x4E,
  0x54, 0x45, 0x52, 0x4E, 0x45, 0x54, 0x22, 0x2F,
  0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E,
  0x61, 0x6D, 0x65, 0x3D, 0x22, 0x54, 0x4F, 0x2D,
  0x4E, 0x41, 0x50, 0x49, 0x44, 0x22, 0x20, 0x76,
  0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22, 0x4E, 0x41,
  0x50, 0x31, 0x22, 0x2F, 0x3E, 0x3C, 0x63, 0x68,
  0x61, 0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69,
  0x73, 0x74, 0x69, 0x63, 0x20, 0x74, 0x79, 0x70,
  0x65, 0x3D, 0x22, 0x50, 0x4F, 0x52, 0x54, 0x22,
  0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E,
  0x61, 0x6D, 0x65, 0x3D, 0x22, 0x50, 0x4F, 0x52,
  0x54, 0x4E, 0x42, 0x52, 0x22, 0x20, 0x76, 0x61,
  0x6C, 0x75, 0x65, 0x3D, 0x22, 0x39, 0x32, 0x30,
  0x33, 0x22, 0x2F, 0x3E, 0x3C, 0x2F, 0x63, 0x68,
  0x61, 0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69,
  0x73, 0x74, 0x69, 0x63, 0x3E, 0x3C, 0x2F, 0x63,
  0x68, 0x61, 0x72, 0x61, 0x63, 0x74, 0x65, 0x72,
  0x69, 0x73, 0x74, 0x69, 0x63, 0x3E, 0x3C, 0x2F,
  0x63, 0x68, 0x61, 0x72, 0x61, 0x63, 0x74, 0x65,
  0x72, 0x69, 0x73, 0x74, 0x69, 0x63, 0x3E, 0x3C,
  0x63, 0x68, 0x61, 0x72, 0x61, 0x63, 0x74, 0x65,
  0x72, 0x69, 0x73, 0x74, 0x69, 0x63, 0x20, 0x74,
  0x79, 0x70, 0x65, 0x3D, 0x22, 0x4E, 0x41, 0x50,
  0x44, 0x45, 0x46, 0x22, 0x3E, 0x3C, 0x70, 0x61,
  0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D,
  0x22, 0x4E, 0x41, 0x50, 0x49, 0x44, 0x22, 0x20,
  0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22, 0x4E,
  0x41, 0x50, 0x31, 0x22, 0x2F, 0x3E, 0x3C, 0x70,
  0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65,
  0x3D, 0x22, 0x42, 0x45, 0x41, 0x52, 0x45, 0x52,
  0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D,
  0x22, 0x47, 0x53, 0x4D, 0x2D, 0x43, 0x53, 0x44,
  0x22, 0x2F, 0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D,
  0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22, 0x4E,
  0x41, 0x4D, 0x45, 0x22, 0x20, 0x76, 0x61, 0x6C,
  0x75, 0x65, 0x3D, 0x22, 0x4D, 0x59, 0x20, 0x49,
  0x53, 0x50, 0x20, 0x43, 0x53, 0x44, 0x22, 0x2F,
  0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E,
  0x61, 0x6D, 0x65, 0x3D, 0x22, 0x4E, 0x41, 0x50,
  0x2D, 0x41, 0x44, 0x44, 0x52, 0x45, 0x53, 0x53,
  0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D,
  0x22, 0x2B, 0x33, 0x35, 0x38, 0x30, 0x38, 0x31,
  0x32, 0x34, 0x30, 0x30, 0x32, 0x22, 0x2F, 0x3E,
  0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61,
  0x6D, 0x65, 0x3D, 0x22, 0x4E, 0x41, 0x50, 0x2D,
  0x41, 0x44, 0x44, 0x52, 0x54, 0x59, 0x50, 0x45,
  0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D,
  0x22, 0x45, 0x31, 0x36, 0x34, 0x22, 0x2F, 0x3E,
  0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61,
  0x6D, 0x65, 0x3D, 0x22, 0x43, 0x41, 0x4C, 0x4C,
  0x54, 0x59, 0x50, 0x45, 0x22, 0x20, 0x76, 0x61,
  0x6C, 0x75, 0x65, 0x3D, 0x22, 0x41, 0x4E, 0x41,
  0x4C, 0x4F, 0x47, 0x2D, 0x4D, 0x4F, 0x44, 0x45,
  0x4D, 0x22, 0x2F, 0x3E, 0x3C, 0x63, 0x68, 0x61,
  0x72, 0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73,
  0x74, 0x69, 0x63, 0x20, 0x74, 0x79, 0x70, 0x65,
  0x3D, 0x22, 0x4E, 0x41, 0x50, 0x41, 0x55, 0x54,
  0x48, 0x49, 0x4E, 0x46, 0x4F, 0x22, 0x3E, 0x3C,
  0x70, 0x61, 0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D,
  0x65, 0x3D, 0x22, 0x41, 0x55, 0x54, 0x48, 0x54,
  0x59, 0x50, 0x45, 0x22, 0x20, 0x76, 0x61, 0x6C,
  0x75, 0x65, 0x3D, 0x22, 0x50, 0x41, 0x50, 0x22,
  0x2F, 0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D, 0x20,
  0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22, 0x41, 0x55,
  0x54, 0x48, 0x4E, 0x41, 0x4D, 0x45, 0x22, 0x20,
  0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22, 0x77,
  0x77, 0x77, 0x6D, 0x6D, 0x6D, 0x75, 0x73, 0x65,
  0x72, 0x22, 0x2F, 0x3E, 0x3C, 0x70, 0x61, 0x72,
  0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22,
  0x41, 0x55, 0x54, 0x48, 0x53, 0x45, 0x43, 0x52,
  0x45, 0x54, 0x22, 0x20, 0x76, 0x61, 0x6C, 0x75,
  0x65, 0x3D, 0x22, 0x77, 0x77, 0x77, 0x6D, 0x6D,
  0x6D, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x22,
  0x2F, 0x3E, 0x3C, 0x2F, 0x63, 0x68, 0x61, 0x72,
  0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73, 0x74,
  0x69, 0x63, 0x3E, 0x3C, 0x63, 0x68, 0x61, 0x72,
  0x61, 0x63, 0x74, 0x65, 0x72, 0x69, 0x73, 0x74,
  0x69, 0x63, 0x20, 0x74, 0x79, 0x70, 0x65, 0x3D,
  0x22, 0x56, 0x41, 0x4C, 0x49, 0x44, 0x49, 0x54,
  0x59, 0x22, 0x3E, 0x3C, 0x70, 0x61, 0x72, 0x6D,
  0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D, 0x22, 0x43,
  0x4F, 0x55, 0x4E, 0x54, 0x52, 0x59, 0x22, 0x20,
  0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D, 0x22, 0x32,
  0x32, 0x38, 0x22, 0x2F, 0x3E, 0x3C, 0x70, 0x61,
  0x72, 0x6D, 0x20, 0x6E, 0x61, 0x6D, 0x65, 0x3D,
  0x22, 0x4E, 0x45, 0x54, 0x57, 0x4F, 0x52, 0x4B,
  0x22, 0x20, 0x76, 0x61, 0x6C, 0x75, 0x65, 0x3D,
  0x22, 0x30, 0x30, 0x31, 0x22, 0x2F, 0x3E, 0x3C,
  0x2F, 0x63, 0x68, 0x61, 0x72, 0x61, 0x63, 0x74,
  0x65, 0x72, 0x69, 0x73, 0x74, 0x69, 0x63, 0x3E,
  0x3C, 0x2F, 0x63, 0x68, 0x61, 0x72, 0x61, 0x63,
  0x74, 0x65, 0x72, 0x69, 0x73, 0x74, 0x69, 0x63,
  0x3E, 0x3C, 0x2F, 0x77, 0x61, 0x70, 0x2D, 0x70,
  0x72, 0x6F, 0x76, 0x69, 0x73, 0x69, 0x6F, 0x6E,
  0x69, 0x6E, 0x67, 0x64, 0x6F, 0x63, 0x3E
]);

/*
 * Test data from OMA-TS-WAP_ProvCont-V1_1-20090421-C.pdf, Appendix C
 */
let wbxml_data_array = new Uint8Array([
  // WBXML version 1.3
  0x03,
  // The Public Identifier for "-//WAPFORUM//DTD PROV 1.0//EN"
  0x0b,
  // Character set UTF-8
  0x6a,
  // String table length = 05
  0x05,
  // String table: 'N', 'A', 'P', '1', 00
  0x4e, 0x41, 0x50, 0x31, 0x00,
  // <wap-provisioningdoc version="1.0">
  0xc5, 0x46, 0x01,
  //   <characteristic type="PXLOGICAL">
  0xc6, 0x51, 0x01,
  //     <parm name="PROXY-ID" value="170.187.51.4"/>
  0x87, 0x15, 0x06, 0x03, 0x31, 0x37, 0x30, 0x2e,
  0x31, 0x38, 0x37, 0x2e, 0x35, 0x31, 0x2e, 0x34,
  0x00, 0x01,
  //     <parm name="NAME" value="BankMainProxy"/>
  0x87, 0x07, 0x06, 0x03, 0x42, 0x61, 0x6e, 0x6b,
  0x4d, 0x61, 0x69, 0x6e, 0x50, 0x72, 0x6f, 0x78,
  0x79, 0x00, 0x01,
  //     <parm name="STARTPAGE" value="http://www.bank.com/startpage.wml"/>
  0x87, 0x1c, 0x06, 0x03, 0x68, 0x74, 0x74, 0x70,
  0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x62,
  0x61, 0x6e, 0x6b, 0x2e, 0x63, 0x6f, 0x6d, 0x2f,
  0x73, 0x74, 0x61, 0x72, 0x74, 0x70, 0x61, 0x67,
  0x65, 0x2e, 0x77, 0x6d, 0x6c, 0x00, 0x01,
  //     <characteristic type="PXAUTHINFO">
  0xc6, 0x59, 0x01,
  //       <parm name="PXAUTH-TYPE" value="HTTP-BASIC"/>
  0x87, 0x19, 0x06, 0x9c, 0x01,
  //       <parm name="PXAUTH-ID" value="pxusername"/>
  0x87, 0x1a, 0x06, 0x03, 0x70, 0x78, 0x75, 0x73,
  0x65, 0x72, 0x6e, 0x61, 0x6d, 0x65, 0x00, 0x01,
  //       <parm name="PXAUTH-PW" value="pxuserpasswd"/>
  0x87, 0x1b, 0x06, 0x03, 0x70, 0x78, 0x75, 0x73,
  0x65, 0x72, 0x70, 0x61, 0x73, 0x73, 0x77, 0x64,
  0x00, 0x01,
  //     </characteristic>
  0x01,
  //     <characteristic type="PXPHYSICAL">
  0xc6, 0x52, 0x01,
  //       <parm name="PHYSICAL-PROXY-ID" value="PROXY 1"/>
  0x87, 0x2f, 0x06, 0x03, 0x50, 0x52, 0x4f, 0x58,
  0x59, 0x20, 0x31, 0x00, 0x01,
  //       <parm name="DOMAIN" value="www.bank.com/"/>
  0x87, 0x17, 0x06, 0x03, 0x77, 0x77, 0x77, 0x2e,
  0x62, 0x61, 0x6e, 0x6b, 0x2e, 0x63, 0x6f, 0x6d,
  0x2f, 0x00, 0x01,
  //       <parm name="PXADDR" value="170.187.51.3"/>
  0x87, 0x20, 0x06, 0x03, 0x31, 0x37, 0x30, 0x2e,
  0x31, 0x38, 0x37, 0x2e, 0x35, 0x31, 0x2e, 0x33,
  0x00, 0x01,
  //       <parm name="PXADDRTYPE" value="IPV4"/>
  0x87, 0x21, 0x06, 0x85, 0x01,
  //       <parm name="TO-NAPID" value="INTERNET"/>
  0x87, 0x22, 0x06, 0x03, 0x49, 0x4e, 0x54, 0x45,
  0x52, 0x4e, 0x45, 0x54, 0x00, 0x01,
  //       <parm name="TO-NAPID" value="NAP1"/>
  0x87, 0x22, 0x06, 0x83, 0x00, 0x01,
  //       <characteristic type="PORT">
  0xc6, 0x53, 0x01,
  //         <parm name="PORTNBR" value="9203"/>
  0x87, 0x23, 0x06, 0x03, 0x39, 0x32, 0x30, 0x33,
  0x00, 0x01,
  //       </characteristic>
  0x01,
  //     </characteristic>
  0x01,
  //   </characteristic>
  0x01,
  //   <characteristic type="NAPDEF">
  0xc6, 0x55, 0x01,
  //     <parm name="NAPID" value="NAP1"/>
  0x87, 0x11, 0x06, 0x83, 0x00, 0x01,
  //     <parm name="BEARER" value="GSM-CSD"/>
  0x87, 0x10, 0x06, 0xaa, 0x01,
  //     <parm name="NAME" value="MY ISP CSD"/>
  0x87, 0x07, 0x06, 0x03, 0x4d, 0x59, 0x20, 0x49,
  0x53, 0x50, 0x20, 0x43, 0x53, 0x44, 0x00, 0x01,
  //     <parm name="NAP-ADDRESS" value="+35808124002"/>
  0x87, 0x08, 0x06, 0x03, 0x2b, 0x33, 0x35, 0x38,
  0x30, 0x38, 0x31, 0x32, 0x34, 0x30, 0x30, 0x32,
  0x00, 0x01,
  //     <parm name="NAP-ADDRTYPE" value="E164"/>
  0x87, 0x09, 0x06, 0x87, 0x01,
  //     <parm name="CALLTYPE" value="ANALOG-MODEM"/>
  0x87, 0x0a, 0x06, 0x90, 0x01,
  //     <characteristic type="NAPAUTHINFO">
  0xc6, 0x5a, 0x01,
  //       <parm name="AUTHTYPE" value="PAP"/>
  0x87, 0x0c, 0x06, 0x9a, 0x01,
  //       <parm name="AUTHNAME" value="wwwmmmuser"/>
  0x87, 0x0d, 0x06, 0x03, 0x77, 0x77, 0x77, 0x6d,
  0x6d, 0x6d, 0x75, 0x73, 0x65, 0x72, 0x00, 0x01,
  //       <parm name="AUTHSECRET" value="wwwmmmsecret"/>
  0x87, 0x0e, 0x06, 0x03, 0x77, 0x77, 0x77, 0x6d,
  0x6d, 0x6d, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74,
  0x00, 0x01,
  //     </characteristic>
  0x01,
  //     <characteristic type="VALIDITY">
  0xc6, 0x54, 0x01,
  //       <parm name="COUNTRY" value="228"/>
  0x87, 0x12, 0x06, 0x03, 0x32, 0x32, 0x38, 0x00,
  0x01,
  //       <parm name="NETWORK" value="001"/>
  0x87, 0x13, 0x06, 0x03, 0x30, 0x30, 0x31, 0x00,
  0x01,
  //     </characteristic>
  0x01,
  //   </characteristic>
  0x01,
  // </wap-provisioningdoc>
  0x01
]);

/*
 * Test data from CHT CP, with code page change
 */
let wbxml_code_page_data_array = new Uint8Array([
  0x03, 0x0B, 0x6A, 0x00, 0x45, 0xC6, 0x56, 0x01,
  0x87, 0x07, 0x06, 0x03, 0x43, 0x48, 0x54, 0x5F,
  0x65, 0x6D, 0x6F, 0x6D, 0x65, 0x00, 0x01, 0x01,
  0xC6, 0x00, 0x01, 0x55, 0x01, 0x87, 0x36, 0x00,
  0x00, 0x06, 0x03, 0x77, 0x32, 0x00, 0x01, 0x87,
  0x00, 0x01, 0x39, 0x00, 0x00, 0x06, 0x03, 0x57,
  0x50, 0x52, 0x4F, 0x58, 0x59, 0x00, 0x01, 0x87,
  0x07, 0x06, 0x03, 0x43, 0x48, 0x54, 0x5F, 0x65,
  0x6D, 0x6F, 0x6D, 0x65, 0x00, 0x01, 0xC6, 0x00,
  0x01, 0x59, 0x01, 0x87, 0x3A, 0x00, 0x00, 0x06,
  0x03, 0x68, 0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F,
  0x77, 0x61, 0x70, 0x2E, 0x65, 0x6D, 0x6F, 0x6D,
  0x65, 0x2E, 0x6E, 0x65, 0x74, 0x2F, 0x00, 0x01,
  0x87, 0x07, 0x06, 0x03, 0x43, 0x48, 0x54, 0x5F,
  0x65, 0x6D, 0x6F, 0x6D, 0x65, 0x00, 0x01, 0x87,
  0x1C, 0x01, 0x01, 0x01, 0xC6, 0x00, 0x01, 0x55,
  0x01, 0x87, 0x36, 0x00, 0x00, 0x06, 0x03, 0x77,
  0x34, 0x00, 0x01, 0x87, 0x00, 0x01, 0x39, 0x00,
  0x00, 0x06, 0x03, 0x4D, 0x50, 0x52, 0x4F, 0x58,
  0x59, 0x00, 0x01, 0x87, 0x00, 0x01, 0x34, 0x00,
  0x00, 0x06, 0x03, 0x68, 0x74, 0x74, 0x70, 0x3A,
  0x2F, 0x2F, 0x6D, 0x6D, 0x73, 0x3A, 0x38, 0x30,
  0x30, 0x32, 0x00, 0x01, 0x01, 0xC6, 0x51, 0x01,
  0x87, 0x15, 0x06, 0x03, 0x57, 0x50, 0x52, 0x4F,
  0x58, 0x59, 0x00, 0x01, 0x87, 0x07, 0x06, 0x03,
  0x43, 0x48, 0x54, 0x5F, 0x65, 0x6D, 0x6F, 0x6D,
  0x65, 0x00, 0x01, 0x87, 0x1C, 0x06, 0x03, 0x68,
  0x74, 0x74, 0x70, 0x3A, 0x2F, 0x2F, 0x77, 0x61,
  0x70, 0x2E, 0x65, 0x6D, 0x6F, 0x6D, 0x65, 0x2E,
  0x6E, 0x65, 0x74, 0x2F, 0x00, 0x01, 0xC6, 0x52,
  0x01, 0x87, 0x2F, 0x06, 0x03, 0x50, 0x52, 0x4F,
  0x58, 0x59, 0x31, 0x00, 0x01, 0x87, 0x20, 0x06,
  0x03, 0x31, 0x30, 0x2E, 0x31, 0x2E, 0x31, 0x2E,
  0x31, 0x00, 0x01, 0x87, 0x21, 0x06, 0x85, 0x01,
  0x87, 0x22, 0x06, 0x03, 0x43, 0x48, 0x54, 0x5F,
  0x65, 0x6D, 0x6F, 0x6D, 0x65, 0x00, 0x01, 0xC6,
  0x53, 0x01, 0x87, 0x23, 0x06, 0x03, 0x38, 0x30,
  0x38, 0x30, 0x00, 0x01, 0x01, 0x01, 0x01, 0xC6,
  0x51, 0x01, 0x87, 0x15, 0x06, 0x03, 0x4D, 0x50,
  0x52, 0x4F, 0x58, 0x59, 0x00, 0x01, 0x87, 0x07,
  0x06, 0x03, 0x43, 0x48, 0x54, 0x5F, 0x4D, 0x4D,
  0x53, 0x00, 0x01, 0xC6, 0x52, 0x01, 0x87, 0x2F,
  0x06, 0x03, 0x50, 0x52, 0x4F, 0x58, 0x59, 0x32,
  0x00, 0x01, 0x87, 0x20, 0x06, 0x03, 0x31, 0x30,
  0x2E, 0x31, 0x2E, 0x31, 0x2E, 0x31, 0x00, 0x01,
  0x87, 0x21, 0x06, 0x85, 0x01, 0x87, 0x22, 0x06,
  0x03, 0x43, 0x48, 0x54, 0x5F, 0x4D, 0x4D, 0x53,
  0x00, 0x01, 0xC6, 0x53, 0x01, 0x87, 0x23, 0x06,
  0x03, 0x38, 0x30, 0x38, 0x30, 0x00, 0x01, 0x01,
  0x01, 0x01, 0xC6, 0x55, 0x01, 0x87, 0x11, 0x06,
  0x03, 0x43, 0x48, 0x54, 0x5F, 0x65, 0x6D, 0x6F,
  0x6D, 0x65, 0x00, 0x01, 0x87, 0x07, 0x06, 0x03,
  0x43, 0x48, 0x54, 0x5F, 0x65, 0x6D, 0x6F, 0x6D,
  0x65, 0x00, 0x01, 0x87, 0x10, 0x06, 0xAB, 0x01,
  0x87, 0x08, 0x06, 0x03, 0x65, 0x6D, 0x6F, 0x6D,
  0x65, 0x00, 0x01, 0x87, 0x09, 0x06, 0x89, 0x01,
  0x01, 0xC6, 0x55, 0x01, 0x87, 0x11, 0x06, 0x03,
  0x43, 0x48, 0x54, 0x5F, 0x4D, 0x4D, 0x53, 0x00,
  0x01, 0x87, 0x07, 0x06, 0x03, 0x43, 0x48, 0x54,
  0x5F, 0x4D, 0x4D, 0x53, 0x00, 0x01, 0x87, 0x10,
  0x06, 0xAB, 0x01, 0x87, 0x08, 0x06, 0x03, 0x65,
  0x6D, 0x6F, 0x6D, 0x65, 0x00, 0x01, 0x87, 0x09,
  0x06, 0x89, 0x01, 0x01, 0x01
]);

/*
 * Test data from OMA-TS-WAP_ProvCont-V1_1-20090421-C.pdf, clause 6.1
 */
let xml_header =
  "<?xml version=\"1.0\"?>" +
  "<!DOCTYPE wap-provisioningdoc PUBLIC \"-//WAPFORUM//DTD PROV 1.0//EN\" \"http://www.wapforum.org/DTD/prov.dtd\">";

let xml_body =
  "<wap-provisioningdoc version=\"1.0\">" +
    "<characteristic type=\"PXLOGICAL\">" +
      "<parm name=\"PROXY-ID\" value=\"170.187.51.4\"/>" +
      "<parm name=\"NAME\" value=\"BankMainProxy\"/>" +
      "<parm name=\"STARTPAGE\" value=\"http://www.bank.com/startpage.wml\"/>" +
      "<characteristic type=\"PXAUTHINFO\">" +
        "<parm name=\"PXAUTH-TYPE\" value=\"HTTP-BASIC\"/>" +
        "<parm name=\"PXAUTH-ID\" value=\"pxusername\"/>" +
        "<parm name=\"PXAUTH-PW\" value=\"pxuserpasswd\"/>" +
      "</characteristic>" +
      "<characteristic type=\"PXPHYSICAL\">" +
        "<parm name=\"PHYSICAL-PROXY-ID\" value=\"PROXY 1\"/>" +
        "<parm name=\"DOMAIN\" value=\"www.bank.com/\"/>" +
        "<parm name=\"PXADDR\" value=\"170.187.51.3\"/>" +
        "<parm name=\"PXADDRTYPE\" value=\"IPV4\"/>" +
        "<parm name=\"TO-NAPID\" value=\"INTERNET\"/>" +
        "<parm name=\"TO-NAPID\" value=\"NAP1\"/>" +
        "<characteristic type=\"PORT\">" +
          "<parm name=\"PORTNBR\" value=\"9203\"/>" +
        "</characteristic>" +
      "</characteristic>" +
    "</characteristic>" +
    "<characteristic type=\"NAPDEF\">" +
      "<parm name=\"NAPID\" value=\"NAP1\"/>" +
      "<parm name=\"BEARER\" value=\"GSM-CSD\"/>" +
      "<parm name=\"NAME\" value=\"MY ISP CSD\"/>" +
      "<parm name=\"NAP-ADDRESS\" value=\"+35808124002\"/>" +
      "<parm name=\"NAP-ADDRTYPE\" value=\"E164\"/>" +
      "<parm name=\"CALLTYPE\" value=\"ANALOG-MODEM\"/>" +
      "<characteristic type=\"NAPAUTHINFO\">" +
        "<parm name=\"AUTHTYPE\" value=\"PAP\"/>" +
        "<parm name=\"AUTHNAME\" value=\"wwwmmmuser\"/>" +
        "<parm name=\"AUTHSECRET\" value=\"wwwmmmsecret\"/>" +
      "</characteristic>" +
      "<characteristic type=\"VALIDITY\">" +
        "<parm name=\"COUNTRY\" value=\"228\"/>" +
        "<parm name=\"NETWORK\" value=\"001\"/>" +
      "</characteristic>" +
    "</characteristic>" +
  "</wap-provisioningdoc>";

let wbxml_code_page_content =
    "<wap-provisioningdoc>" +
    "<characteristic type=\"BOOTSTRAP\">" +
      "<parm name=\"NAME\" value=\"CHT_emome\"/>" +
    "</characteristic>" +
    "<characteristic type=\"APPLICATION\">" +
      "<parm name=\"APPID\" value=\"w2\"/>" +
      "<parm name=\"TO-PROXY\" value=\"WPROXY\"/>" +
      "<parm name=\"NAME\" value=\"CHT_emome\"/>" +
      "<characteristic type=\"RESOURCE\">" +
        "<parm name=\"URI\" value=\"http://wap.emome.net/\"/>" +
        "<parm name=\"NAME\" value=\"CHT_emome\"/>" +
        "<parm name=\"STARTPAGE\"/>" +
      "</characteristic>" +
    "</characteristic>" +
    "<characteristic type=\"APPLICATION\">" +
      "<parm name=\"APPID\" value=\"w4\"/>" +
      "<parm name=\"TO-PROXY\" value=\"MPROXY\"/>" +
      "<parm name=\"ADDR\" value=\"http://mms:8002\"/>" +
    "</characteristic>" +
    "<characteristic type=\"PXLOGICAL\">" +
      "<parm name=\"PROXY-ID\" value=\"WPROXY\"/>" +
      "<parm name=\"NAME\" value=\"CHT_emome\"/>" +
      "<parm name=\"STARTPAGE\" value=\"http://wap.emome.net/\"/>" +
      "<characteristic type=\"PXPHYSICAL\">" +
        "<parm name=\"PHYSICAL-PROXY-ID\" value=\"PROXY1\"/>" +
        "<parm name=\"PXADDR\" value=\"10.1.1.1\"/>" +
        "<parm name=\"PXADDRTYPE\" value=\"IPV4\"/>" +
        "<parm name=\"TO-NAPID\" value=\"CHT_emome\"/>" +
        "<characteristic type=\"PORT\">" +
          "<parm name=\"PORTNBR\" value=\"8080\"/>" +
        "</characteristic>" +
      "</characteristic>" +
    "</characteristic>" +
    "<characteristic type=\"PXLOGICAL\">" +
      "<parm name=\"PROXY-ID\" value=\"MPROXY\"/>" +
      "<parm name=\"NAME\" value=\"CHT_MMS\"/>" +
      "<characteristic type=\"PXPHYSICAL\">" +
        "<parm name=\"PHYSICAL-PROXY-ID\" value=\"PROXY2\"/>" +
        "<parm name=\"PXADDR\" value=\"10.1.1.1\"/>" +
        "<parm name=\"PXADDRTYPE\" value=\"IPV4\"/>" +
        "<parm name=\"TO-NAPID\" value=\"CHT_MMS\"/>" +
        "<characteristic type=\"PORT\">" +
          "<parm name=\"PORTNBR\" value=\"8080\"/>" +
        "</characteristic>" +
      "</characteristic>" +
    "</characteristic>" +
    "<characteristic type=\"NAPDEF\">" +
      "<parm name=\"NAPID\" value=\"CHT_emome\"/>" +
      "<parm name=\"NAME\" value=\"CHT_emome\"/>" +
      "<parm name=\"BEARER\" value=\"GSM-GPRS\"/>" +
      "<parm name=\"NAP-ADDRESS\" value=\"emome\"/>" +
      "<parm name=\"NAP-ADDRTYPE\" value=\"APN\"/>" +
    "</characteristic>" +
    "<characteristic type=\"NAPDEF\">" +
      "<parm name=\"NAPID\" value=\"CHT_MMS\"/>" +
      "<parm name=\"NAME\" value=\"CHT_MMS\"/>" +
      "<parm name=\"BEARER\" value=\"GSM-GPRS\"/>" +
      "<parm name=\"NAP-ADDRESS\" value=\"emome\"/>" +
      "<parm name=\"NAP-ADDRTYPE\" value=\"APN\"/>" +
    "</characteristic>" +
    "</wap-provisioningdoc>";

/**
 * CP in plain text
 *
 * Test case from OMA-TS-WAP_ProvCont-V1_1-20090421-C.pdf, clause 6.1
 */
add_test(function test_cp_parse_plain_text() {
  test_parser(text_data_array, "text/vnd.wap.connectivity-xml", {
    contentType: "text/vnd.wap.connectivity-xml",
    content: xml_header + xml_body
  });

  run_next_test();
});

/**
 * CP compressed by WBXML
 *
 * Test case from OMA-TS-WAP_ProvCont-V1_1-20090421-C.pdf, Appendix C
 */
add_test(function test_cp_parse_wbxml() {
  test_parser(wbxml_data_array, "application/vnd.wap.connectivity-wbxml", {
    contentType: "text/vnd.wap.connectivity-xml",
    content: xml_body
  });

  run_next_test();
});

/**
 * CP compressed by WBXML with VENDORCONFIG
 */
add_test(function test_cp_parse_wbxml() {
  let wbxml_vendor_config_data_array = new Uint8Array([
    0x03, 0x0b, 0x6a, 0x05, 0x4e, 0x41, 0x50, 0x31,
    0x00, 0xC5, 0x46, 0x01, 0xc6, 0x57, 0x01, 0x01
  ]);

  test_parser(wbxml_vendor_config_data_array, "application/vnd.wap.connectivity-wbxml", {
                contentType: "application/vnd.wap.connectivity-wbxml",
                content: wbxml_vendor_config_data_array
              });

  run_next_test();
});

/**
 * CP compressed by WBXML with code page switch
 */
add_test(function test_cp_parse_wbxml_code_page() {
  test_parser(wbxml_code_page_data_array, "application/vnd.wap.connectivity-wbxml", {
                contentType: "text/vnd.wap.connectivity-xml",
                content: wbxml_code_page_content
              });

  run_next_test();
});

/**
 * HMAC test
 */
add_test(function test_cp_hmac_userpin() {
  test_hmac(wbxml_code_page_data_array,
            "AA2DC41FC48AEEF3FED7351B1EE704461A8894D4",
            "0000",
            {
              checked: true,
              pass: true
            });

  run_next_test();
});

add_test(function test_cp_hmac_networkpin() {
  let wbxml_empty_data_array = new Uint8Array([
    0x03, 0x0b, 0x6a, 0x00, 0x45, 0x01
  ]);

  test_hmac(wbxml_empty_data_array,
            "1AF545FE2823DC9347064450F90FF1BBF957E146",
            CP.Authenticator.formatImsi("466923103145252"),
            {
              checked: true,
              pass: true
            });

  run_next_test();
});
