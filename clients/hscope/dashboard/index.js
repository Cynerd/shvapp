let websocket = null;
const ws_uri = "wss://nirvana.elektroline.cz:3778";
const user = "hscope";
const password = "holyshit!";

const txt_log = document.getElementById("txt_log");
document.getElementById("toggle_log").onclick = () => {
	if (txt_log.className === "hide") {
		txt_log.className = "";
		txt_log.scrollTop = txt_log.scrollHeight;
	} else {
		txt_log.className = "hide";
	}
};

const debug = (...args) => {
	if (txt_log) {
		txt_log.value += args.join(" ") + "\n";
		txt_log.scrollTop = txt_log.scrollHeight;
	}
};

const format_severity = (value) => {
	switch (value) {
		case "ok":
			return "🟢";
		case "warn":
			return "🟠";
		case "error":
			return "🔴";
		default:
			return "unknown severity";
	}
};


const resolve_hscope_tree = (path, container) => {

	websocket.callRpcMethod(path, "dir").then((methods) => {
		if (methods.rpcValue.value[2].value.some(x => x.value === "run")) {
			const nodeContainer = document.createElement("tr");
			container.appendChild(nodeContainer);
			const runCellElement = document.createElement("td");
			runCellElement.style.textAlign = "center";
			const runElement = document.createElement("button");
			runElement.innerText = "Run";
			runElement.onclick = () => {
				websocket.callRpcMethod(path, "run");
			};
			runCellElement.appendChild(runElement);
			nodeContainer.appendChild(runCellElement);

			const pathElement = document.createElement("td");
			pathElement.innerText = path;
			nodeContainer.appendChild(pathElement);
			const severityElement = document.createElement("td");
			severityElement.className = "center-text";
			const messageElement = document.createElement("td");
			messageElement.className = "center-text";
			const timeChangedElement = document.createElement("td");
			timeChangedElement.className = "center-text";
			const lastRunElement = document.createElement("td");
			lastRunElement.className = "center-text";

			let should_animate = false;
			const animate_element = (elem) => {
				if (should_animate) {
					elem.classList.add("animate-change");
					// This dance refreshes the animation.
					// https://stackoverflow.com/a/45036752
					elem.style.animation = "none";
					elem.offsetHeight;
					elem.style.animation = null;
				}
			};

			const updateElements = (value) => {
				if (typeof value.severity !== "undefined") {
					severityElement.innerText = format_severity(value.severity.value);
				} else {
					severityElement.innerText = "❓";
				}
				animate_element(severityElement);

				if (typeof value.message !== "undefined") {
					messageElement.innerText = value.message.value;
				} else {
					messageElement.innerText = "";
				}
				animate_element(messageElement);

				if (typeof value.time_changed !== "undefined") {
					timeChangedElement.innerText = new Date(value.time_changed.value.epochMsec).toLocaleString([]);
				} else {
					timeChangedElement.innerText = "";
				}
				animate_element(timeChangedElement);
			};

			const got_first_status = websocket.callRpcMethod(path + "/status", "get").then((value) => {
				updateElements(value.rpcValue.value[2].value);
			});

			websocket.subscribe(path + "/status", "chng", (changedPath, type, value) => {
				updateElements(value[1].value);
			});

			const got_first_last_run = websocket.callRpcMethod(path + "/lastRunTimestamp", "get").then((value) => {
				lastRunElement.innerText = new Date(value.rpcValue.value[2].value.epochMsec).toLocaleString([]);
			});

			Promise.all([got_first_status, got_first_last_run]).then(() => {
				should_animate = true;
			});

			websocket.subscribe(path + "/lastRunTimestamp", "chng", (changedPath, type, value) => {
				lastRunElement.innerText = new Date(value[1].value.epochMsec).toLocaleString([]);
				animate_element(lastRunElement);
			});

			nodeContainer.appendChild(severityElement);
			nodeContainer.appendChild(messageElement);
			nodeContainer.appendChild(timeChangedElement);
			nodeContainer.appendChild(lastRunElement);
		}
	})

	websocket.callRpcMethod(path, "ls").then((paths) => {
		paths.rpcValue.value[2].value.forEach((childPath) => {
			resolve_hscope_tree(path + "/" + childPath, container);
		});
	});
};

const send_ping = () => {
	websocket.callRpcMethod(".broker/app", "ping");
};

const connect_websocket = () => {
	try {
		websocket = new WsClient({
			user,
			password,
			wsUri: ws_uri,
			requestHandler: (rpc_msg) => {
					const method = rpc_msg.method().asString();
					const resp = new RpcMessage();
					if (method === "dir") {
						resp.setResult(["ls", "dir", "appName"]);
					} else if (method === "ls") {
						resp.setResult([]);
					} else if (method === "appName") {
						resp.setResult("hscope-client");
					} else {
						debug('ERROR: ' + "Method: " + method + " is not defined.");
						resp.setError("Method: " + method + " is not defined.");
					}
					resp.setRequestId(rpc_msg.requestId());
					resp.setCallerIds(rpc_msg.callerIds());
					send_rpc_message(resp);
			},
			logDebug: debug,
			onConnected: () => {
				setInterval(send_ping, 1000 * 30);
				resolve_hscope_tree("hscope", hscope_container);
			}
		});
	} catch (exception) {
		debug('EXCEPTION: ' + exception);
	}
}

connect_websocket();
