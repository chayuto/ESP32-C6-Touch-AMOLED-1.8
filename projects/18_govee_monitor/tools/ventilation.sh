#!/bin/sh
# Should you open the window?
#
# The only honest way to answer is ABSOLUTE humidity (g of water per m3 of air),
# never relative humidity. RH is a ratio against what the air could hold at its
# current temperature, and cold air holds very little -- so 5 C outdoor air at
# 90 % RH carries ~6 g/m3 while an 18 C room at 78 % RH carries ~11.5 g/m3.
# Opening up there DRIES the room, even though the outdoor number is the larger
# one. Trusting RH would tell you to keep the window shut all winter, which is
# exactly backwards.
#
# Ventilating swaps room air for outdoor air, so it removes moisture whenever
# outdoor g/m3 is below indoor g/m3. The catch is the incoming air is usually
# colder: it cools the room and its surfaces, and a cold surface condenses even
# when the air is drier. So the verdict below also reports the temperature cost.
#
#   ./tools/ventilation.sh            # now (last 15 min)
#   ./tools/ventilation.sh --profile  # add a 24 h hourly outdoor comparison
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
[ -f "$DIR/../.env" ] && { set -a; . "$DIR/../.env"; set +a; }
[ -n "$DATABASE_URL" ] || { echo "DATABASE_URL not set (see .env)"; exit 1; }

psql "$DATABASE_URL" -X -q -P pager=off <<'SQL'
set time zone 'Australia/Brisbane';

create temporary view v_now as
with latest as (
  select r.mac, r.ts, r.temp_c, r.humid,
         row_number() over (partition by r.mac order by r.ts desc) rn
  from reading r
  where r.ts > now() - interval '15 minutes')
select s.label, coalesce(s.placement,'indoor') placement, l.temp_c t, l.humid h,
       6.112*exp(17.67*l.temp_c/(l.temp_c+243.5))*l.humid*2.1674/(273.15+l.temp_c) ah,
       243.04*(ln(l.humid/100.0)+17.625*l.temp_c/(243.04+l.temp_c))
             /(17.625-(ln(l.humid/100.0)+17.625*l.temp_c/(243.04+l.temp_c))) dp
from latest l join sensor s on s.mac = l.mac
where l.rn = 1;

\echo '=== current readings ==='
select label, placement,
       round(h::numeric,1) "RH %", round(t::numeric,1) "air C",
       round(dp::numeric,1) "dew pt", round(ah::numeric,2) "g/m3"
from v_now order by placement desc, ah desc;

\echo ''
\echo '=== verdict ==='
select
  case when (select count(*) from v_now where placement='outdoor') = 0
       then 'NO OUTDOOR SENSOR YET -- set sensor.placement = ''outdoor'' on it, '
            || 'then this answers per room.'
       else null end as note
where (select count(*) from v_now where placement='outdoor') = 0;

with o as (select ah, t, dp from v_now where placement='outdoor' limit 1),
     i as (select label, ah, t, dp from v_now where placement='indoor')
select i.label,
       round(i.ah::numeric,2)          "room g/m3",
       round(o.ah::numeric,2)          "out g/m3",
       round((o.ah - i.ah)::numeric,2) "delta",
       round((o.t - i.t)::numeric,1)   "temp cost C",
       case when o.ah < i.ah - 0.5 then 'OPEN - outdoor air is drier'
            when o.ah > i.ah + 0.5 then 'KEEP SHUT - outdoor air is wetter'
            else 'NO BENEFIT - within sensor error' end as verdict
from i cross join o order by (o.ah - i.ah);
SQL

if [ "$1" = "--profile" ]; then
psql "$DATABASE_URL" -X -q -P pager=off <<'SQL'
set time zone 'Australia/Brisbane';
\echo ''
\echo '=== last 24 h: when is outdoor air driest? (g/m3) ==='
select to_char(date_trunc('hour',r.ts),'DD HH24') hr,
  round(avg(6.112*exp(17.67*r.temp_c/(r.temp_c+243.5))*r.humid*2.1674/(273.15+r.temp_c))
        filter (where coalesce(s.placement,'indoor')='outdoor')::numeric,2) outdoor,
  round(avg(6.112*exp(17.67*r.temp_c/(r.temp_c+243.5))*r.humid*2.1674/(273.15+r.temp_c))
        filter (where coalesce(s.placement,'indoor')='indoor')::numeric,2) indoor_avg
from reading r join sensor s on s.mac=r.mac
where r.ts > now() - interval '24 hours'
group by 1 order by 1;
SQL
fi
