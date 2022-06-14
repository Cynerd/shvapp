local shv_utils = require("shv_utils")
return function (set_status, path_to_agent, filesystem_path)
	if path_to_agent == nil then
		error("path_to_agent mustn't be null")
	end

	if filesystem_path == nil then
		error("filesystem_path mustn't be null")
	end

	local do_test = function ()
		shv.rpc_call(path_to_agent, "runScript", string.format([[df %s | sed -n -r '1n;s/[^ ]+ +[^ ]+ +[^ ]+ +[^ ]+ +([^ ]+)%% +[^\n ]+/\1/;p']], filesystem_path), function (result, error)
			if error then
				shv_utils.handle_rpc_error(error, set_status)
				return
			end
			local percent = tonumber(result.value)
			shv_utils.print_table(result.meta)
			local res = {
				message = tostring(percent) .. "% usage"
			}

			res.severity = "ok"
			if percent >= 75 then
				res.severity = "warn"
			end
			if percent >= 90 then
				res.severity = "error"
			end

			set_status(res)
		end)
	end

	shv.add_timer(do_test, 1000 * 60 * 60 * 24) -- Every day

	return do_test
end
