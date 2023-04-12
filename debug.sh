echo $$
kill -STOP $$
exec /bin/bash -c "./paren > >(pv -q > /dev/null)"
