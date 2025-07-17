CREATE OR REPLACE FUNCTION hello_world(text)
RETURNS text AS $$
    SELECT 'Hello, ' || $1;
$$ LANGUAGE sql;

CREATE OR REPLACE FUNCTION hello_world_c(text)
RETURNS text
AS 'MODULE_PATHNAME', 'hello_world_c'
LANGUAGE C STRICT;
