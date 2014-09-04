/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_HEAD_JS = "stk_helper.js";

function testSendDTMF(command, expect) {
  log("STK CMD " + JSON.stringify(command));
  is(command.typeOfCommand, iccManager.STK_CMD_SEND_DTMF, expect.name);
  is(command.commandQualifier, expect.commandQualifier, expect.name);
  is(command.options.text, expect.text, expect.name);

  let icons = command.options.icons;
  if (icons) {
    isIcons(icons, expect.icons, expect.name);

    let iconSelfExplanatory = command.options.iconSelfExplanatory;
    is(iconSelfExplanatory, expect.iconSelfExplanatory, expect.name);
  }

  runNextTest();
}

let tests = [
  {command: "d01b810301140082028183850953656e642044544d46ac052143658709",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_1_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF"}},
  {command: "d010810301140082028183ac052143658709",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_1_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d0138103011400820281838500ac06c1cccccccc2c",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_2_with_alpha_identifier",
            commandQualifier: 0x00,
            text: ""}},
  {command: "d011810301140082028183ac06c1cccccccc2c",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_2_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d01d810301140082028183850a42617369632049636f6eac02c1f29e020001",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_3_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Basic Icon",
            iconSelfExplanatory: true,
            icons: [basicIcon]}},
  {command: "d011810301140082028183ac02c1f29e020005",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_3_without_alpha_identifier",
            commandQualifier: 0x00,
            iconSelfExplanatory: true,
            icons: [colorTransparencyIcon]}},
  {command: "d01c810301140082028183850953656e642044544d46ac02c1f29e020101",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_4_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF",
            iconSelfExplanatory: false,
            icons: [basicIcon]}},
  {command: "d011810301140082028183ac02c1f29e020105",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_4_without_alpha_identifier",
            commandQualifier: 0x00,
            iconSelfExplanatory: false,
            icons: [colorTransparencyIcon]}},
  {command: "d028810301140082028183851980041704140420041004120421042204120423041904220415ac02c1f2",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_5_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "ЗДРАВСТВУЙТЕ"}},
  {command: "d00d810301140082028183ac02c1f2",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_5_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b00b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_6_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b00b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_6_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d01d810301140082028183850b53656e642044544d462032ac052143658709",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_7_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 2"}},
  // send_dtmf_cmd_7_without_alpha_identifier has the same pdu as
  // send_dtmf_cmd_1_without_alpha_identifier.
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b01b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_8_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b01b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_8_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d00400b002b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_9_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d00400b002b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_9_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b04b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_10_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b04b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_10_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462032ac052143658709d004000b00b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_11_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 2"}},
  // send_dtmf_cmd_11_without_alpha_identifier has the same pdu as
  // send_dtmf_cmd_6_without_alpha_identifier.
  {command: "d01d810301140082028183850b53656e642044544d462033ac052143658709",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_12_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 3"}},
  // send_dtmf_cmd_12_without_alpha_identifier has the same pdu as
  // send_dtmf_cmd_1_without_alpha_identifier.
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b08b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_13_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b08b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_13_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b10b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_14_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b10b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_14_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b20b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_15_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b20b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_15_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b40b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_16_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b40b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_16_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d023810301140082028183850b53656e642044544d462031ac052143658709d004000b80b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_17_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "Send DTMF 1"}},
  {command: "d016810301140082028183ac052143658709d004000b80b4",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_17_without_alpha_identifier",
            commandQualifier: 0x00}},
  {command: "d0148103011400820281838505804f60597dac02c1f2",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_18_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "你好"}},
  // send_dtmf_cmd_18_without_alpha_identifier has the same pdu as
  // send_dtmf_cmd_5_without_alpha_identifier.
  {command: "d01281030114008202818385038030ebac02c1f2",
   func: testSendDTMF,
   expect: {name: "send_dtmf_cmd_19_with_alpha_identifier",
            commandQualifier: 0x00,
            text: "ル"}}
  // send_dtmf_cmd_19_without_alpha_identifier has the same pdu as
  // send_dtmf_cmd_5_without_alpha_identifier.
];

runNextTest();
