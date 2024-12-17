#include "utils.h"
#include <shv/chainpack/rpcvalue.h>

namespace mock_sites {
// TODO: Name this properly
const auto fin_slave_broker = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "eyas":{
    "_meta":{
      "FL":{"hiddenNode":true}
    },
    "opc":{
      "_meta":{
        "HP3":{"syncPath":".app/shvjournal"},
        "name":"Raide Jokeri depot shvgate",
        "type":"DepotG2"
      }
    },
    "with_app_history":{
      "_meta":{
        "HP3":{}
      }
    }
  }
}
)"_cpon;

const auto fin_master_broker = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "fin":{
    "_meta":{"name":"Finland", "name_cz":"Finsko"},
    "hel":{
      "_meta":{"name":"Helsinki", "name_cz":"Helsinki"},
      "tram":{
        "hel002":{
          "_meta":{
            "HP3":{"type":"HP3"}
          },
          "eyas":{
            "_meta":{
              "FL":{"hiddenNode":true}
            },
            "opc":{
              "_meta":{
                "HP3":{"syncPath":".app/shvjournal"},
                "name":"Raide Jokeri depot shvgate",
                "type":"DepotG2"
              }
            }
          }
        }
      }
    }
  }
}
)"_cpon;

const auto pushlog_hp = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "pushlog":{
    "_meta":{
      "HP3":{"pushLog": true}
    }
  }
}
)"_cpon;

const auto master_hp_with_slave_pushlog = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "master": {
    "_meta":{
      "HP3":{"type": "HP3"}
    },
    "pushlog":{
      "_meta":{
        "HP3":{"pushLog": true}
      }
    }
  }
}
)"_cpon;

const auto legacy_hp = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "legacy": {
    "_meta":{
      "HP":{}
    },
  }
}
)"_cpon;

const auto one_device = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "one": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal"}
    },
  },
}
)"_cpon;

const auto two_sites_inside_some_node = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "some_node": {
    "site_one": {
      "_meta":{
        "HP3":{"syncPath":".app/shvjournal"}
      },
    },
    "site_two": {
      "_meta":{
        "HP3":{"syncPath":".app/shvjournal"}
      },
    }
  }
}
)"_cpon;

const auto two_devices = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "one": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal"}
    },
  },
  "two": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal"}
    },
  }
}
)"_cpon;

const auto even_more_devices = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "one": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal"}
    },
  },
  "two": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal"}
    },
  },
  "mil014atm": {
    "_meta":{
      "HP3":{"type": "HP3"}
    },
    "sig": {
      "HP3":{"syncPath":".app/shvjournal"}
    }
  }
}
)"_cpon;

const auto some_site = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "some_site": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal"}
    },
  },
}
)"_cpon;

const auto site_with_custom_chunk_limit = R"(
{
  "_meta":{
    "HP3":{"type": "HP3"}
  },
  "some_site": {
    "_meta":{
      "HP3":{"syncPath":".app/shvjournal", "readLogChunkLimit": 149}
    },
  },
}
)"_cpon;
}


namespace mock_typeinfo {
const auto one_device = RpcValue::stringToBlob(R"EOF(
<"version":4>{
	"deviceDescriptions":{
		"one_alarm_device":{
			"properties":[
				{
					"name":"status",
					"typeName":"one_alarm_type"
				}
			]
		},
	},
	"devicePaths":{
		"one":"one_alarm_device",
	},
	"types":{
		"one_alarm_type":{
			"fields":[
				{"alarm":"error", "description":"Alarm 1", "label":"Alarm 1 label", "name":"some_alarm_name", "value":0},
			],
			"typeName":"BitField"
		},
	}
}
)EOF");
const auto two_device = RpcValue::stringToBlob(R"EOF(
<"version":4>{
	"deviceDescriptions":{
		"one_alarm_device":{
			"properties":[
				{
					"name":"status",
					"typeName":"one_alarm_type"
				}
			]
		},
	},
	"devicePaths":{
		"one":"one_alarm_device",
		"two":"one_alarm_device",
	},
	"types":{
		"one_alarm_type":{
			"fields":[
				{"alarm":"error", "description":"Alarm 1", "label":"Alarm 1 label", "name":"some_alarm_name", "value":0},
			],
			"typeName":"BitField"
		},
	}
}
)EOF");
const auto two_alarms = RpcValue::stringToBlob(R"EOF(
<"version":4>{
	"deviceDescriptions":{
		"two_alarm_device":{
			"properties":[
				{
					"name":"status1",
					"typeName":"one_alarm_type"
				},
				{
					"name":"status2",
					"typeName":"one_alarm_type"
				}
			]
		},
	},
	"devicePaths":{
		"one":"two_alarm_device",
	},
	"types":{
		"one_alarm_type":{
			"fields":[
				{"alarm":"error", "description":"Alarm 1", "label":"Alarm 1 label", "name":"some_alarm_name", "value":0},
			],
			"typeName":"BitField"
		},
	}
}
)EOF");
const auto different_severity = RpcValue::stringToBlob(R"EOF(
<"version":4>{
	"deviceDescriptions":{
		"two_alarm_device":{
			"properties":[
				{
					"name":"status1",
					"typeName":"alarm_warning"
				},
				{
					"name":"status2",
					"typeName":"alarm_error"
				}
			]
		},
	},
	"devicePaths":{
		"one":"two_alarm_device",
	},
	"types":{
		"alarm_warning":{
			"fields":[
				{"alarm":"warning", "description":"Alarm 1", "label":"Alarm 1 label", "name":"alarm_warning", "value":0},
			],
			"typeName":"BitField"
		},
		"alarm_error":{
			"fields":[
				{"alarm":"error", "description":"Alarm 1", "label":"Alarm 1 label", "name":"alarm_error", "value":0},
			],
			"typeName":"BitField"
		},
	}
}
)EOF");
}
