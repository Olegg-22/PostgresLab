CREATE OR REPLACE FUNCTION my_schedule(
    status TEXT,         -- 'temp' или 'absl'
    time_str TEXT,     -- 'HH:MM:SS'
    message TEXT       -- произвольный текст
)
RETURNS void
AS 'MODULE_PATHNAME', 'my_schedule'
LANGUAGE C STRICT;
