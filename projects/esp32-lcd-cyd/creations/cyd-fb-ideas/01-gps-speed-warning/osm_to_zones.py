#!/usr/bin/env python3
"""
（進階、可選）由 OpenStreetMap 攞某個範圍嘅道路限速 (maxspeed)，轉做 zones.csv
用法：python3 osm_to_zones.py 22.30 114.15 22.35 114.20 > zones.csv
         (南緯 西經 北緯 東經 = 一個長方形範圍，唔好揀太大)
原理：每條有 maxspeed 嘅路，取佢嘅中間點做圓心，半徑 = 路長一半（最少 50m）
呢個係簡化做法；原作者係將成條路切成 tile 存入 SD 卡，準確好多。
只用 Python 標準庫，唔使裝其他嘢。
"""
import json, math, sys, urllib.parse, urllib.request

def dist(a, b):
    R = 6371000
    dlat = math.radians(b[0] - a[0]); dlon = math.radians(b[1] - a[1])
    h = math.sin(dlat/2)**2 + math.cos(math.radians(a[0]))*math.cos(math.radians(b[0]))*math.sin(dlon/2)**2
    return 2 * R * math.asin(math.sqrt(h))

s, w, n, e = sys.argv[1:5]
q = f'[out:json][timeout:60];way["highway"]["maxspeed"]({s},{w},{n},{e});out geom;'
url = "https://overpass-api.de/api/interpreter?data=" + urllib.parse.quote(q)
data = json.load(urllib.request.urlopen(url))

print("lat,lon,radius_m,limit,name")
for way in data["elements"]:
    pts = [(p["lat"], p["lon"]) for p in way.get("geometry", [])]
    sp = way["tags"].get("maxspeed", "").split()[0]
    if not pts or not sp.isdigit():
        continue
    length = sum(dist(pts[i], pts[i+1]) for i in range(len(pts)-1))
    mid = pts[len(pts)//2]
    name = way["tags"].get("name:en", way["tags"].get("ref", "road")).replace(",", " ")[:23]
    print(f"{mid[0]:.6f},{mid[1]:.6f},{max(50, int(length/2))},{sp},{name}")
