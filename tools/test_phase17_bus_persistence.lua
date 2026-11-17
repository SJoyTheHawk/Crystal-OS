local timer
local joint={route="101",operator="JOINT",bound="I",service_type="1",destination="Kwun Tong",
  parts={{operator="KMB",route="101",bound="I",service_type="1"},
    {operator="CTB",route="101",bound="O",service_type="2"}},
  stop_id="000001",stop_operator="KMB",stop_seq=2,stop_name="Joint Stop",stop_lat=22.3,stop_lon=114.1}
local favorites={"broken",{route="4",operator="KMB",stop_seq=1},joint,
  {route="101",operator="JOINT",bound="I",service_type="1",destination="Kwun Tong",
    parts=joint.parts,stop_id="000001",stop_operator="KMB",stop_seq=2,stop_name="Duplicate"},
  {route="2",operator="CTB",bound="O",service_type="1",destination="Central",
    stop_id="002737",stop_operator="CTB",stop_seq=1,stop_name="Central Stop"}}
for route=3,9 do
  favorites[#favorites+1]={route=tostring(route),operator="KMB",bound="O",service_type="1",
    destination="Destination",stop_id="KMB-"..route,stop_operator="KMB",stop_seq=1,stop_name="Stop "..route}
end
local saved={favorites=favorites,recent_routes={
  {route="1A",operator="KMB",bound="O",service_type="1",destination="Star Ferry"},
  {route="1A",operator="KMB",bound="O",service_type="1",destination="Duplicate"},
  "bad",{route="101",operator="JOINT",parts={{operator="KMB"}}}
}}
local assets={
  ["assets/catalog/runtime.txt"]="L 2\nC 1\nK keypad.txt 4\nQ route-search.txt 4 1\n",
  ["assets/catalog/keypad.txt"]="\n\t2\n",
  ["assets/catalog/route-search.txt"]="2\tX\n"
}
crystal={ui=ui,json={decode=decode_json},fs={read=function(path)return assets[path] end},
  state={get=function(key)return saved[key] end,set=function(key,value)saved[key]=value;return true end},
  service={time={now=function()return 1794810600 end},http={
    fetch=function()return nil,"unexpected request" end,cancel=function()return false end
  }}}
ui.timer=function(ms,callback)assert(ms==100);timer=callback;return 1 end
local function ticks(count)
  for _=1,count do run_callback(timer) end
end
local original_favorites=saved.favorites
local original_recents=saved.recent_routes
local app=assert(loadfile(package_path))()
assert(app.onCreate())
assert(has_text("Some saved stops were skipped"))
assert(has_text("101   JOINT") and has_text("Joint Stop"))
assert(has_text("2   CTB") and has_text("Central Stop"))
assert(has_text("8   KMB") and not has_text("9   KMB"))
assert(saved.favorites==original_favorites and saved.recent_routes==original_recents,
  "loading must not silently rewrite the stored lists")
click("Search")
ticks(8)
assert(has_text("1A   KMB") and has_text("Some recent routes were skipped"))
assert(text_count("1A   KMB")==1,"duplicate recent routes must collapse to one identity")
assert(not has_text("101   JOINT"),"the malformed recent route must be skipped")
assert(app.onDestroy())
print("PASS: persisted favorites/recents validation, duplicate removal and bounds")
