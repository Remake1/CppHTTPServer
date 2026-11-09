-- wrk script: optionally sends "Connection: close", and prints the results as
-- one JSON line (prefixed with BENCH_JSON) for bench/benchmark.py to parse.
--
--   wrk -s bench/wrk_report.lua http://127.0.0.1:3000/ [-- close]

local close_conn = false

function init(args)
    for _, a in ipairs(args) do
        if a == "close" then close_conn = true end
    end
    if close_conn then
        wrk.headers["Connection"] = "close"
    end
end

function done(summary, latency, requests)
    local e = summary.errors
    local pct = {}
    for _, p in ipairs({50, 75, 90, 99, 99.9}) do
        pct[#pct + 1] = string.format('"%s": %d', tostring(p), latency:percentile(p))
    end
    io.write(string.format(
        'BENCH_JSON {"duration_us": %d, "requests": %d, "bytes": %d, ' ..
        '"errors": {"connect": %d, "read": %d, "write": %d, "status": %d, "timeout": %d}, ' ..
        '"latency_us": {"min": %d, "mean": %.1f, "max": %d, "stdev": %.1f, %s}}\n',
        summary.duration, summary.requests, summary.bytes,
        e.connect, e.read, e.write, e.status, e.timeout,
        latency.min, latency.mean, latency.max, latency.stdev, table.concat(pct, ", ")))
end
