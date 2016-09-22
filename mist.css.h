const char *mist_css = 
  "\n.mistvideo {\n  background: black center none no-repeat;\n  /*LTS\n  backgroun" \
  "d-size: auto 30%;\n  background-image: url('data:image/svg+xml;base64,PD94bWwgdm" \
  "Vyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB4bWxuczpkYz" \
  "0iaHR0cDovL3B1cmwub3JnL2RjL2VsZW1lbnRzLzEuMS8iIHhtbG5zOmNjPSJodHRwOi8vY3JlYXRpdm" \
  "Vjb21tb25zLm9yZy9ucyMiIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZG" \
  "Ytc3ludGF4LW5zIyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM9Im" \
  "h0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiBpZD0ic3ZnNTI4MCIgaGVpZ2h0PSIxNTAiIHdpZHRoPS" \
  "IyMDIuNTIwMTciIHZlcnNpb249IjEuMSI+PGRlZnMgaWQ9ImRlZnM1MjgyIiAvPjxtZXRhZGF0YSBpZD" \
  "0ibWV0YWRhdGE1Mjg1Ij48cmRmOlJERj48Y2M6V29yayByZGY6YWJvdXQ9IiI+PGRjOmZvcm1hdD5pbW" \
  "FnZS9zdmcreG1sPC9kYzpmb3JtYXQ+PGRjOnR5cGUgcmRmOnJlc291cmNlPSJodHRwOi8vcHVybC5vcm" \
  "cvZGMvZGNtaXR5cGUvU3RpbGxJbWFnZSIgLz48ZGM6dGl0bGU+PC9kYzp0aXRsZT48L2NjOldvcms+PC" \
  "9yZGY6UkRGPjwvbWV0YWRhdGE+PGcgaWQ9ImxheWVyMSIgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoLTQ4Lj" \
  "cyNjIzNCwtNDQ5Ljc1MjA0KSI+PHBhdGggc3R5bGU9ImZpbGw6IzhjYjNjZjtmaWxsLW9wYWNpdHk6MT" \
  "tmaWxsLXJ1bGU6bm9uemVybztzdHJva2U6bm9uZSIgaWQ9InBhdGg0NiIgZD0ibSA2OS4wODIxNTMsNT" \
  "M0LjI1MzE0IDgxLjAyODYzNyw0My4yNjQ4OCA4MS4wMjYwNSwtNDMuMjY0ODggLTMwLjUwNzA3LC02OS" \
  "4wMDg5NCAtMTAxLjAzOTA2OCwwIC0zMC41MDg1NDksNjkuMDA4OTQgMCwwIHogbSA4MS4wMjg2MzcsNT" \
  "AuMTE3MzIgLTg4LjgyNjQ4NiwtNDcuNDMwODggMzQuMzY5NDE4LC03Ny43NDA3OSAxMDguOTEzNTY4LD" \
  "AgMzQuMzY5MDUsNzcuNzQwNzkgLTg4LjgyNTU1LDQ3LjQzMDg4IDAsMCIgLz48cGF0aCBzdHlsZT0iZm" \
  "lsbDojYjVkM2UyO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lIiBpZD" \
  "0icGF0aDQ4IiBkPSJtIDIzNC44NzM1Miw1MzguMDM5NTkgLTE2Ni43NTE1MTksMCAwLC02LjA0NTQxID" \
  "E2Ni43NTE1MTksMCAwLDYuMDQ1NDEiIC8+PHBhdGggc3R5bGU9ImZpbGw6IzhjYjNjZjtmaWxsLW9wYW" \
  "NpdHk6MTtmaWxsLXJ1bGU6bm9uemVybztzdHJva2U6bm9uZSIgaWQ9InBhdGg1MCIgZD0ibSA4My40OD" \
  "cwMDksNTM1LjUyMDgyIGMgMCw5LjU5OTY4IC03Ljc4MTA4LDE3LjM4MDc1IC0xNy4zNzk2NTEsMTcuMz" \
  "gwNzUgLTkuNTk5MTIyLDAgLTE3LjM4MTEyNCwtNy43ODEwNiAtMTcuMzgxMTI0LC0xNy4zODA3NSAwLC" \
  "05LjU5Nzg0IDcuNzgyMDAyLC0xNy4zODA3NiAxNy4zODExMjQsLTE3LjM4MDc2IDkuNTk4NTcxLDAgMT" \
  "cuMzc5NjUxLDcuNzgyOTIgMTcuMzc5NjUxLDE3LjM4MDc2IiAvPjxwYXRoIHN0eWxlPSJmaWxsOiM4Y2" \
  "IzY2Y7ZmlsbC1vcGFjaXR5OjE7ZmlsbC1ydWxlOm5vbnplcm87c3Ryb2tlOm5vbmUiIGlkPSJwYXRoNT" \
  "IiIGQ9Im0gMjUxLjI0NjQsNTM1LjUyMDgyIGMgMCw5LjU5OTY4IC03Ljc4MTA3LDE3LjM4MDc1IC0xNy" \
  "4zODA3NCwxNy4zODA3NSAtOS41OTk2OCwwIC0xNy4zNzg5MiwtNy43ODEwNiAtMTcuMzc4OTIsLTE3Lj" \
  "M4MDc1IDAsLTkuNTk3ODQgNy43NzkyNCwtMTcuMzgwNzYgMTcuMzc4OTIsLTE3LjM4MDc2IDkuNTk5Nj" \
  "csMCAxNy4zODA3NCw3Ljc4MjkyIDE3LjM4MDc0LDE3LjM4MDc2IiAvPjxwYXRoIHN0eWxlPSJmaWxsOi" \
  "NiNWQzZTI7ZmlsbC1vcGFjaXR5OjE7ZmlsbC1ydWxlOm5vbnplcm87c3Ryb2tlOm5vbmUiIGlkPSJwYX" \
  "RoNTQiIGQ9Im0gMTU1Ljc4NzY4LDUzNy4xODI4IC01LjA1MjI4LC0zLjMyMjEyIDQ5LjM3MTA4LC03NS" \
  "4wNjM1NiA1LjA1MDQzLDMuMzIyMTEgLTQ5LjM2OTIzLDc1LjA2MzU3IDAsMCIgLz48cGF0aCBzdHlsZT" \
  "0iZmlsbDojYjVkM2UyO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lIi" \
  "BpZD0icGF0aDU2IiBkPSJtIDE0OC43ODYsNTM3Ljc4MTYyIC01Mi44OTY5NzcsLTc0LjA1NTY5IDQuOT" \
  "E5NDE3LC0zLjUxNTU4IDUyLjg5NTMzLDc0LjA1NzU0IC00LjkxNzc3LDMuNTEzNzMgMCwwIiAvPjxwYX" \
  "RoIHN0eWxlPSJmaWxsOiM4Y2IzY2Y7ZmlsbC1vcGFjaXR5OjE7ZmlsbC1ydWxlOm5vbnplcm87c3Ryb2" \
  "tlOm5vbmUiIGlkPSJwYXRoNTgiIGQ9Im0gMjE0LjU5NjI4LDQ2Mi41OTgyOSBjIDAsNy4wOTc1IC01Lj" \
  "c1MjQyLDEyLjg0ODEgLTEyLjg0NjI0LDEyLjg0ODEgLTcuMDk1NjUsMCAtMTIuODQ2MjUsLTUuNzUwNi" \
  "AtMTIuODQ2MjUsLTEyLjg0ODEgMCwtNy4wOTM4MSA1Ljc1MDYsLTEyLjg0NjI1IDEyLjg0NjI1LC0xMi" \
  "44NDYyNSA3LjA5MzgyLDAgMTIuODQ2MjQsNS43NTI0NCAxMi44NDYyNCwxMi44NDYyNSIgLz48cGF0aC" \
  "BzdHlsZT0iZmlsbDojOGNiM2NmO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZT" \
  "pub25lIiBpZD0icGF0aDYwIiBkPSJtIDExMS44MjQ4NCw0NjIuNTk4MjkgYyAwLDcuMDk3NSAtNS43NT" \
  "EzMywxMi44NDgxIC0xMi44NDU4NzMsMTIuODQ4MSAtNy4wOTUyODUsMCAtMTIuODQ3NTMyLC01Ljc1MD" \
  "YgLTEyLjg0NzUzMiwtMTIuODQ4MSAwLC03LjA5MzgxIDUuNzUyMjQ3LC0xMi44NDYyNSAxMi44NDc1Mz" \
  "IsLTEyLjg0NjI1IDcuMDk0NTQzLDAgMTIuODQ1ODczLDUuNzUyNDQgMTIuODQ1ODczLDEyLjg0NjI1Ii" \
  "AvPjxwYXRoIHN0eWxlPSJmaWxsOiM4Y2IzY2Y7ZmlsbC1vcGFjaXR5OjE7ZmlsbC1ydWxlOm5vbnplcm" \
  "87c3Ryb2tlOm5vbmUiIGlkPSJwYXRoNjIiIGQ9Im0gMTY4Ljg3NzE0LDQ4Ny41MzUzNSBjIDAsOS4zOT" \
  "E0NSAtNy42MTE1NywxNy4wMDQ4NyAtMTcuMDAxMTksMTcuMDA0ODcgLTkuMzg5NjMsMCAtMTcuMDAzMD" \
  "QsLTcuNjEzNDIgLTE3LjAwMzA0LC0xNy4wMDQ4NyAwLC05LjM4OTYyIDcuNjEzNDEsLTE3LjAwMTIgMT" \
  "cuMDAzMDQsLTE3LjAwMTIgOS4zODk2MiwwIDE3LjAwMTE5LDcuNjExNTggMTcuMDAxMTksMTcuMDAxMi" \
  "IgLz48cGF0aCBzdHlsZT0iZmlsbDojYjVkM2UyO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub256ZX" \
  "JvO3N0cm9rZTpub25lIiBpZD0icGF0aDY0IiBkPSJtIDE2NS44NTUzNiw1MzYuNDAxNTYgYyAwLDcuNz" \
  "IzOTYgLTYuMjU5MTMsMTMuOTgxMjYgLTEzLjk3OTQxLDEzLjk4MTI2IC03LjcyMDI5LDAgLTEzLjk4MT" \
  "I2LC02LjI1NzMgLTEzLjk4MTI2LC0xMy45ODEyNiAwLC03LjcyMDI5IDYuMjYwOTcsLTEzLjk3OTQxID" \
  "EzLjk4MTI2LC0xMy45Nzk0MSA3LjcyMDI4LDAgMTMuOTc5NDEsNi4yNTkxMiAxMy45Nzk0MSwxMy45Nz" \
  "k0MSIgLz48cGF0aCBzdHlsZT0iZmlsbDojOGNiM2NmO2ZpbGwtb3BhY2l0eToxO2ZpbGwtcnVsZTpub2" \
  "56ZXJvO3N0cm9rZTpub25lIiBpZD0icGF0aDY2IiBkPSJtIDY4LjMzNTczNiw1MzcuNDA1NzQgLTIuOT" \
  "Q1ODY2LC01LjI4MDc0IDgzLjg3ODg4LC00Ny45ODU0NyA0LjQ1NzEyLDYuNzkxNjIgLTg1LjM5MDEzNC" \
  "w0Ni40NzQ1OSAwLDAiIC8+PHBhdGggc3R5bGU9ImZpbGw6IzhjYjNjZjtmaWxsLW9wYWNpdHk6MTtmaW" \
  "xsLXJ1bGU6bm9uemVybztzdHJva2U6bm9uZSIgaWQ9InBhdGg2OCIgZD0ibSAxNDkuNDY5NTgsNDkwLj" \
  "Y1NDc3IC01Mi4xNDE3MjQsLTI1LjMxNDc2IDIuNTQ1NjY5LC01LjQ4MTU5IDUzLjY1MzM1NSwyMy44MD" \
  "IwNCAtMy4zMDE4NSw2LjIzODg3IC0wLjc1NTQ1LDAuNzU1NDQiIC8+PHBhdGggc3R5bGU9ImZpbGw6Iz" \
  "hjYjNjZjtmaWxsLW9wYWNpdHk6MTtmaWxsLXJ1bGU6bm9uemVybztzdHJva2U6bm9uZSIgaWQ9InBhdG" \
  "g3MCIgZD0ibSAxNDkuNDE0Myw0ODQuMDYzOTcgNTEuMDA3MjYsLTIzLjgwMjAzIDIuNjU1MSw1LjQyOT" \
  "k5IC00OS40OTYzNSwyNS4zMTQ3NyIgLz48cGF0aCBzdHlsZT0iZmlsbDojOGNiM2NmO2ZpbGwtb3BhY2" \
  "l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lIiBpZD0icGF0aDcyIiBkPSJtIDIzMy4wOT" \
  "M2Miw1MzguNTA3NTggLTgzLjg4MDE2LC00Ny45ODU0NyA0LjU2NzY4LC02LjcyNzE0IDgyLjM2NzQzLD" \
  "Q5LjQ5NjM3IC0zLjA1NDk1LDUuMjE2MjQgMCwwIiAvPjxwYXRoIHN0eWxlPSJmaWxsOiM4Y2IzY2Y7Zm" \
  "lsbC1vcGFjaXR5OjE7ZmlsbC1ydWxlOm5vbnplcm87c3Ryb2tlOm5vbmUiIGlkPSJwYXRoNzQiIGQ9Im" \
  "0gMTU0LjUyMDAxLDU4MC40ODQ1MiAtNi44MDA4NCwwIC0yLjI2NjM0LC05My43MDI3OCAxMS4zMzUzNi" \
  "wwIC0yLjI2ODE4LDkzLjcwMjc4IDAsMCIgLz48cGF0aCBzdHlsZT0iZmlsbDojOGNiM2NmO2ZpbGwtb3" \
  "BhY2l0eToxO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lIiBpZD0icGF0aDc2IiBkPSJtIDE2OC" \
  "4xMjE2OSw1ODIuMzczMTMgYyAwLDkuNTk5NjggLTcuNzgxMDcsMTcuMzc4OTEgLTE3LjM4MDc1LDE3Lj" \
  "M3ODkxIC05LjU5Nzg0LDAgLTE3LjM3ODkxLC03Ljc3OTIzIC0xNy4zNzg5MSwtMTcuMzc4OTEgMCwtOS" \
  "41OTk2OCA3Ljc4MTA3LC0xNy4zODA3NiAxNy4zNzg5MSwtMTcuMzgwNzYgOS41OTk2OCwwIDE3LjM4MD" \
  "c1LDcuNzgxMDggMTcuMzgwNzUsMTcuMzgwNzYiIC8+PC9nPjwvc3ZnPg==');\n  LTS*/\n  displa" \
  "y: inline-block;\n  color: white;\n  font-family: sans-serif;\n  text-align: cen" \
  "ter;\n}\n.mistvideo[data-loading] {\n  background-image: none;\n  position: rela" \
  "tive;\n  min-width: 70px;\n  min-height: 70px;\n}\n.mistvideo[data-loading]:befo" \
  "re {\n  content: '';\n  display: block;\n  width: 25px;\n  height: 25px;\n  bord" \
  "er: 5px solid transparent;\n  border-radius: 25px;\n  border-top-color: white;\n" \
  "  border-left: 0px;\n  opacity: 0.8;\n  animation: spin 1.5s infinite linear;\n " \
  " margin: -15px 0 0 -12.5px;\n  position: absolute;\n  top: 50%;\n  left: 50%;\n " \
  " z-index: 5;\n}\n.mistvideo .error {\n  margin: 225px 20px 20px;\n}\n.mistvideo " \
  ".error button {\n  margin: 5px auto;\n  display: block;\n}\n.mistplayer {\n  pos" \
  "ition: relative;\n  overflow: hidden;\n}\n.mistplayer[data-hide] {\n  cursor: no" \
  "ne;\n}\n.mistplayer .html5_player {\n  display: block;\n  margin: 0 auto;\n}\n.m" \
  "istplayer .controls {\n  height: 75px;\n  background-color: black;\n  opacity: 0" \
  ".6;\n  position: absolute;\n  left: 1px;\n  right: 1px;\n  bottom: -75px;\n  dis" \
  "play: flex;\n  align-items: center;\n}\n.mistplayer:hover:not([data-hide]) .cont" \
  "rols {\n  bottom: 0;\n}\n.mistplayer.audio {\n  width: 500px;\n}\n.mistplayer.au" \
  "dio .controls {\n  position: static;\n}\n.mistplayer:not(:hover) .controls,\n.mi" \
  "stplayer[data-hide] .controls {\n  transition: bottom 0.5s ease-in 1s;\n}\n.mist" \
  "player video {\n  display: block;\n}\n.mistplayer .controls .row {\n  display: f" \
  "lex;\n  flex-flow: row nowrap;\n}\n.mistplayer .controls .column {\n  display: f" \
  "lex;\n  flex-flow: column nowrap;\n  align-items: center;\n}\n.mistplayer .contr" \
  "ols .row .button {\n\n}\n.mistplayer .controls .row .button:not(:first-child) {\n" \
  "  margin-left: 0;\n}\n.mistplayer .controls .button {\n  cursor: pointer;\n  wid" \
  "th: 45px;\n  line-height: 45px;\n  font-size: 16px;\n  position: relative;\n  ba" \
  "ckground: transparent center none no-repeat;\n}\n.mistplayer .controls .button.p" \
  "lay {\n  height: 45px;\n  margin-left: 15px;\n}\n.mistplayer .controls .button.p" \
  "lay[data-state=playing] {\n  background-image: url(\"data:image/svg+xml;base64,P" \
  "D94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB4b" \
  "WxuczpkYz0iaHR0cDovL3B1cmwub3JnL2RjL2VsZW1lbnRzLzEuMS8iIHhtbG5zOmNjPSJodHRwOi8vY" \
  "3JlYXRpdmVjb21tb25zLm9yZy9ucyMiIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wM" \
  "i8yMi1yZGYtc3ludGF4LW5zIyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIge" \
  "G1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB2ZXJzaW9uPSIxLjEiIGlkPSJzdmcyIiBoZ" \
  "WlnaHQ9IjQ1IiB3aWR0aD0iNDUiPjxkZWZzIGlkPSJkZWZzNCIgLz48bWV0YWRhdGEgaWQ9Im1ldGFkY" \
  "XRhNyI+PHJkZjpSREY+PGNjOldvcmsgcmRmOmFib3V0PSIiPjxkYzpmb3JtYXQ+aW1hZ2Uvc3ZnK3htb" \
  "DwvZGM6Zm9ybWF0PjxkYzp0eXBlIHJkZjpyZXNvdXJjZT0iaHR0cDovL3B1cmwub3JnL2RjL2RjbWl0e" \
  "XBlL1N0aWxsSW1hZ2UiIC8+PGRjOnRpdGxlPjwvZGM6dGl0bGU+PC9jYzpXb3JrPjwvcmRmOlJERj48L" \
  "21ldGFkYXRhPjxnIHRyYW5zZm9ybT0idHJhbnNsYXRlKDAsLTEwMDcuMzYyMikiIGlkPSJsYXllcjEiP" \
  "jxnIHN0eWxlPSJmaWxsOiNmZmYiIHRyYW5zZm9ybT0idHJhbnNsYXRlKDMuMDMwNDU3NSw0Ny43Mjk3M" \
  "DUpIiBpZD0iZzM3NzkiPjxwYXRoIGlkPSJwYXRoMzgyMy03IiBkPSJtIDQuNDY5NTQyOSw5OTguMTYzN" \
  "zcgYSA0LjAwMTE5MTYsNC4wMDExOTE2IDAgMCAwIDMuNzQ5OTk5LDMuOTY4NzMgbCAyLjI4MTI1MDEsM" \
  "CBhIDQuMDAxMTkxNiw0LjAwMTE5MTYgMCAwIDAgMy45Njg3NSwtMy43NTAwMyBsIDAsLTMyLjI4MTIzI" \
  "GEgNC4wMDExOTE2LDQuMDAxMTkxNiAwIDAgMCAtMy43NSwtMy45Njg3NSBsIC0yLjI4MTI1MDEsMCBhI" \
  "DQuMDAxMTkxNiw0LjAwMTE5MTYgMCAwIDAgLTMuOTY4NzQ5LDMuNzUgbCAwLDMyLjI4MTI4IHoiIHN0e" \
  "WxlPSJmaWxsOiNmZmY7ZmlsbC1vcGFjaXR5OjE7c3Ryb2tlOm5vbmUiIC8+PHBhdGggaWQ9InBhdGgzO" \
  "DIzLTctNCIgZD0ibSAyNC40Njk1NDIsOTk4LjE2MzggYSA0LjAwMTE5MTYsNC4wMDExOTE2IDAgMCAwI" \
  "DMuNzUsMy45Njg3IGwgMi4yODEyNSwwIGEgNC4wMDExOTE2LDQuMDAxMTkxNiAwIDAgMCAzLjk2ODc1L" \
  "C0zLjc1IGwgMCwtMzIuMjgxMjYgYSA0LjAwMTE5MTYsNC4wMDExOTE2IDAgMCAwIC0zLjc1LC0zLjk2O" \
  "Dc1IGwgLTIuMjgxMjUsMCBhIDQuMDAxMTkxNiw0LjAwMTE5MTYgMCAwIDAgLTMuOTY4NzUsMy43NSBsI" \
  "DAsMzIuMjgxMzEgeiIgc3R5bGU9ImZpbGw6I2ZmZjtmaWxsLW9wYWNpdHk6MTtzdHJva2U6bm9uZSIgL" \
  "z48L2c+PC9nPjwvc3ZnPg==\");\n}\n.mistplayer .controls .button.play[data-state=pa" \
  "used] {\n  background-image: url(\"data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0" \
  "iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB4bWxuczpkYz0iaHR0cDo" \
  "vL3B1cmwub3JnL2RjL2VsZW1lbnRzLzEuMS8iIHhtbG5zOmNjPSJodHRwOi8vY3JlYXRpdmVjb21tb25" \
  "zLm9yZy9ucyMiIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF" \
  "4LW5zIyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM9Imh0dHA6Ly9" \
  "3d3cudzMub3JnLzIwMDAvc3ZnIiB2ZXJzaW9uPSIxLjEiIGlkPSJzdmcyIiBoZWlnaHQ9IjQ1IiB3aWR" \
  "0aD0iNDUiPjxkZWZzIGlkPSJkZWZzNCIgLz48bWV0YWRhdGEgaWQ9Im1ldGFkYXRhNyI+PHJkZjpSREY" \
  "+PGNjOldvcmsgcmRmOmFib3V0PSIiPjxkYzpmb3JtYXQ+aW1hZ2Uvc3ZnK3htbDwvZGM6Zm9ybWF0Pjx" \
  "kYzp0eXBlIHJkZjpyZXNvdXJjZT0iaHR0cDovL3B1cmwub3JnL2RjL2RjbWl0eXBlL1N0aWxsSW1hZ2U" \
  "iIC8+PGRjOnRpdGxlPjwvZGM6dGl0bGU+PC9jYzpXb3JrPjwvcmRmOlJERj48L21ldGFkYXRhPjxnIHR" \
  "yYW5zZm9ybT0idHJhbnNsYXRlKDAsLTEwMDcuMzYyMikiIGlkPSJsYXllcjEiPjxwYXRoIHRyYW5zZm9" \
  "ybT0ibWF0cml4KDEuMDE0MTgyNywtMC41ODU1Mzg2NywwLjU4NTUzODY3LDEuMDE0MTgyNywtMC40ODQ" \
  "xOTgzMSwxMDIyLjg4OTMpIiBkPSJNIDEwLjMxMjUsLTYuMzQzNzUgQSAyLjk0MTYxODYsMi45NDE2MTg" \
  "2IDAgMCAwIDcuOTA2MjUsLTQuODc1IGwgLTE0LjEyNSwyNC41IGEgMi45NDE2MTg2LDIuOTQxNjE4NiA" \
  "wIDAgMCAyLjU2MjUsNC40MDYyNSBsIDI4LjI4MTI1LDAgQSAyLjk0MTYxODYsMi45NDE2MTg2IDAgMCA" \
  "wIDI3LjE1NjI1LDE5LjYyNSBMIDEzLC00Ljg3NSBhIDIuOTQxNjE4NiwyLjk0MTYxODYgMCAwIDAgLTI" \
  "uNjg3NSwtMS40Njg3NSB6IiBpZD0icGF0aDM4MDkiIHN0eWxlPSJmaWxsOiNmZmY7ZmlsbC1vcGFjaXR" \
  "5OjE7ZmlsbC1ydWxlOmV2ZW5vZGQ7c3Ryb2tlOm5vbmUiIC8+PC9nPjwvc3ZnPg==\");\n}\n.mistp" \
  "layer .controls .progress_container {\n  flex-grow: 1;\n  position: relative;\n " \
  " margin: 15px;\n}\n.mistplayer .controls .button.progress {\n  height: 15px;\n  " \
  "border: 1px solid white;\n  overflow: hidden;\n  width: auto;\n  margin: 0;\n}\n" \
  ".mistplayer .controls .button.progress .bar {\n  background-color: white;\n  pos" \
  "ition: absolute;\n  width: 0;\n  top: 0;\n  bottom: 0;\n  left: 0;\n}\n.mistplay" \
  "er .controls .button.progress .buffer {\n  background-color: white;\n  opacity: " \
  "0.5;\n  position: absolute;\n  top: 0;\n  bottom: 0;\n}\n.mistplayer .controls ." \
  "progress_container .hint {\n  position: absolute;\n  background: white;\n  borde" \
  "r-radius: 5px;\n  bottom: 22px;\n  padding: 3px 5px;\n  color: black;\n  opacity" \
  ": 0.6;\n  display: none;\n  font-size: 12px;\n}\n.mistplayer .controls .progress" \
  "_container .hint:after {\n  content: '';\n  display: block;\n  position: absolut" \
  "e;\n  left: 0;\n  border: 5px solid transparent;\n  border-left-color: white;\n " \
  " bottom: -5px;\n}\n.mistplayer .controls .button.timestamp {\n  width: auto;\n  " \
  "cursor: default;\n  -webkit-user-select: none;\n  -moz-user-select: none;\n  -ms" \
  "-user-select: none;\n  user-select: none;\n}\n.mistplayer .controls .button.soun" \
  "d {\n  height: 65px;\n  width: 30px;\n  margin-left: 20px;\n  position: relative" \
  ";\n  background: url(\"data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmN" \
  "vZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB4bWxuczpkYz0iaHR0cDovL3B1cmwub3J" \
  "nL2RjL2VsZW1lbnRzLzEuMS8iIHhtbG5zOmNjPSJodHRwOi8vY3JlYXRpdmVjb21tb25zLm9yZy9ucyM" \
  "iIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF4LW5zIyIgeG1" \
  "sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3J" \
  "nLzIwMDAvc3ZnIiB2ZXJzaW9uPSIxLjEiIGlkPSJzdmczOTM3IiBoZWlnaHQ9IjY1IiB3aWR0aD0iMzA" \
  "iPjxkZWZzIGlkPSJkZWZzMzkzOSIgLz48bWV0YWRhdGEgaWQ9Im1ldGFkYXRhMzk0MiI+PHJkZjpSREY" \
  "+PGNjOldvcmsgcmRmOmFib3V0PSIiPjxkYzpmb3JtYXQ+aW1hZ2Uvc3ZnK3htbDwvZGM6Zm9ybWF0Pjx" \
  "kYzp0eXBlIHJkZjpyZXNvdXJjZT0iaHR0cDovL3B1cmwub3JnL2RjL2RjbWl0eXBlL1N0aWxsSW1hZ2U" \
  "iIC8+PGRjOnRpdGxlPjwvZGM6dGl0bGU+PC9jYzpXb3JrPjwvcmRmOlJERj48L21ldGFkYXRhPjxnIHR" \
  "yYW5zZm9ybT0idHJhbnNsYXRlKDAsLTk4Ny4zNjIyKSIgaWQ9ImxheWVyMSI+PHBhdGggaWQ9InJlY3Q" \
  "0Njc0IiBkPSJtIDAsMTA1Mi4zNjIyIDAsLTY1IDUsMCBjIC0wLjE3MjU4OCwwIC0wLjMzNzI1NiwwLjA" \
  "yOTUgLTAuNSwwLjA2MjUgLTAuNDg5MTExLDAuMSAtMC45NDIzNDUsMC4zMTcyNSAtMS4yODEyNSwwLjY" \
  "1NjI1IC0wLjIyNjIwNiwwLjIyNjIgLTAuNDA0NzQzLDAuNTEzNSAtMC41MzEyNSwwLjgxMjUgLTAuMTI" \
  "2NTA3LDAuMjk5MSAtMC4xODc1LDAuNjIzNjUgLTAuMTg3NSwwLjk2ODc1IDAsMCAwLjA3NTk3OCwwLjQ" \
  "0NzA1IDAuMTg3NSwwLjc4MTI1IGwgMTkuODQzNzUsNTkuNDk5OTUgYyAwLjE0Mjc3NywxLjI0NTEgMS4" \
  "xODU0MTQsMi4yMTg4IDIuNDY4NzUsMi4yMTg4IGwgLTI1LDAgeiBtIDI1LDAgYyAwLjg3NzU0OSwwIDE" \
  "uNjQ3NjYzLC0wLjQ0MSAyLjA5Mzc1LC0xLjEyNSAwLjA2MzgxLC0wLjA5OCAwLjEwNjIsLTAuMjA0NiA" \
  "wLjE1NjI1LC0wLjMxMjUgMC4wMjk2MiwtMC4wNjIgMC4wNjkyNiwtMC4xMjI1IDAuMDkzNzUsLTAuMTg" \
  "3NSAwLjA0NTAxLC0wLjEyMTIgMC4wNjc0MSwtMC4yNDU5IDAuMDkzNzUsLTAuMzc1IDAuMDA5LC0wLjA" \
  "0NCAwLjAyNDU3LC0wLjA4IDAuMDMxMjUsLTAuMTI1IDAuMDE4NzgsLTAuMTIzNSAwLjAzMTI1LC0wLjI" \
  "0NjIgMC4wMzEyNSwtMC4zNzUgbCAwLC02MCBjIDAsLTEuMzg1IC0xLjExNDk5OSwtMi41IC0yLjUsLTI" \
  "uNSBsIDUsMCAwLDY1IC01LDAgeiIgc3R5bGU9ImNvbG9yOiMwMDAwMDA7ZGlzcGxheTppbmxpbmU7b3Z" \
  "lcmZsb3c6dmlzaWJsZTt2aXNpYmlsaXR5OnZpc2libGU7ZmlsbDojMDAwMDAwO2ZpbGwtb3BhY2l0eTo" \
  "xO2ZpbGwtcnVsZTpub256ZXJvO3N0cm9rZTpub25lO21hcmtlcjpub25lO2VuYWJsZS1iYWNrZ3JvdW5" \
  "kOmFjY3VtdWxhdGUiIC8+PHBhdGggaWQ9InBhdGg0Njk3LTYiIGQ9Im0gMjUsMTA1Mi4zNjE3IGMgLTE" \
  "uMjgzMzM2LDAgLTIuMzI1OTczLC0wLjk3MzcgLTIuNDY4NzUsLTIuMjE4NyBMIDIuNjg3NSw5OTAuNjQ" \
  "yOSBDIDIuNTc1OTc4LDk5MC4zMDg3IDIuNSw5ODkuODYxNyAyLjUsOTg5Ljg2MTcgYyAwLC0wLjM0NTE" \
  "gMC4wNjA5OTMsLTAuNjY5NyAwLjE4NzUsLTAuOTY4OCAwLjEyNjUwNywtMC4yOTkgMC4zMDUwNDQsLTA" \
  "uNTg2MyAwLjUzMTI1LC0wLjgxMjUgMC4zMzg5MDUsLTAuMzM5IDAuNzkyMTM5LC0wLjU1NjIgMS4yODE" \
  "yNSwtMC42NTYyIDAuMTYyNzQ0LC0wLjAzMyAwLjMyNzQxMiwtMC4wNjIgMC41LC0wLjA2MiBsIDIwLDA" \
  "gYyAxLjM4NTAwMSwwIDIuNSwxLjExNSAyLjUsMi41IGwgMCw2MCBjIDAsMC4xMjg4IC0wLjAxMjQ3LDA" \
  "uMjUxNSAtMC4wMzEyNSwwLjM3NSAtMC4wMDY3LDAuMDQ1IC0wLjAyMjI1LDAuMDgxIC0wLjAzMTI1LDA" \
  "uMTI1IC0wLjAyNjM0LDAuMTI5MiAtMC4wNDg3NCwwLjI1MzggLTAuMDkzNzUsMC4zNzUgLTAuMDI0NDk" \
  "sMC4wNjUgLTAuMDY0MTMsMC4xMjUyIC0wLjA5Mzc1LDAuMTg3NSAtMC4wNTAwNSwwLjEwNzkgLTAuMDk" \
  "yNDQsMC4yMTQ1IC0wLjE1NjI1LDAuMzEyNSAtMC40NDYwODcsMC42ODQgLTEuMjE2MjAxLDEuMTI1IC0" \
  "yLjA5Mzc1LDEuMTI1IHogbSAwLC0xLjIxODcgYyAwLjQ3NDEwNiwwIDAuODY0NzM0LC0wLjIxMTQgMS4" \
  "wOTM3NSwtMC41NjI1IC0wLjAyMTEyLDAuMDMyIC0wLjAwNTksLTAuMDEgMC4wNjI1LC0wLjE1NjMgYSA" \
  "xLjIwNDQ1MiwxLjIwNDQ1MiAwIDAgMSAwLC0wLjAzMSBjIDAuMDIzNSwtMC4wNDkgMC4wNTE5OCwtMC4" \
  "wNTIgMC4wNjI1LC0wLjA2MiAwLjAwNTUsLTAuMDE2IDAuMDA5NCwtMC4wMzUgMCwtMC4wMzEgMC4wMDE" \
  "3LC0wLjAxIDAuMDA1NSwtMC4wNjEgMC4wMzEyNSwtMC4xODc1IDAuMDA4LC0wLjAzOSAwLjAyNTU1LC0" \
  "wLjAzOSAwLjAzMTI1LC0wLjA2MiAwLjAwOTgsLTAuMDY2IDAuMDA1NSwtMC4xMDI3IDAsLTAuMDk0IC0" \
  "wLjAwMTYsLTAuMDMgMCwtMC4wNjggMCwtMC4wOTQgbCAwLC02MCBjIDAsLTAuNzM4NiAtMC41NDI2MTc" \
  "sLTEuMjgxMyAtMS4yODEyNSwtMS4yODEzIGwgLTIwLDAgYyAtMC4wMzUzNTMsMCAtMC4xMDUzMjIsMCA" \
  "tMC4yNSwwLjAzMSAtMC4yOTY4NjMsMC4wNjEgLTAuNTQ2MzQzLDAuMTcxMyAtMC42ODc1LDAuMzEyNSA" \
  "tMC4wODkzOTQsMC4wODkgLTAuMjA1MjYzLDAuMjU4IC0wLjI4MTI1LDAuNDM3NSAtMC4wNTUzMTUsMC4" \
  "xMzA4IC0wLjA1ODY2MSwwLjI4MzIgLTAuMDYyNSwwLjQ2ODcgLTIuNTJlLTQsMC4wMTIgMCwwLjAxOSA" \
  "wLDAuMDMxIDAuMDI3OTgyLDAuMTM1MyAwLjA4MjQ5OSwwLjI3ODkgMC4xMjUsMC40MDYyIGwgMTkuODQ" \
  "zNzUsNTkuNTAwNSBhIDEuMjA0NDUyLDEuMjA0NDUyIDAgMCAxIDAuMDMxMjUsMC4yNSBjIDAuMDc1Mjc" \
  "sMC42NTY0IDAuNjA3MDU0LDEuMTI1IDEuMjgxMjUsMS4xMjUgeiIgc3R5bGU9ImZpbGw6I2ZmZmZmZjt" \
  "maWxsLW9wYWNpdHk6MTtzdHJva2U6bm9uZSIgLz48L2c+PC9zdmc+\") no-repeat;\n}\n.mistpla" \
  "yer .controls .button.sound .speaker {\n  width: 25px;\n  height: 25px;\n  margi" \
  "n: 0;\n  background-image: url(\"data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iM" \
  "S4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB4bWxuczpkYz0iaHR0cDovL" \
  "3B1cmwub3JnL2RjL2VsZW1lbnRzLzEuMS8iIHhtbG5zOmNjPSJodHRwOi8vY3JlYXRpdmVjb21tb25zL" \
  "m9yZy9ucyMiIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF4L" \
  "W5zIyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM9Imh0dHA6Ly93d" \
  "3cudzMub3JnLzIwMDAvc3ZnIiB2ZXJzaW9uPSIxLjEiIGlkPSJzdmc0NjU5IiBoZWlnaHQ9IjI1IiB3a" \
  "WR0aD0iMjUiPjxkZWZzIGlkPSJkZWZzNDY2MSIgLz48bWV0YWRhdGEgaWQ9Im1ldGFkYXRhNDY2NCI+P" \
  "HJkZjpSREY+PGNjOldvcmsgcmRmOmFib3V0PSIiPjxkYzpmb3JtYXQ+aW1hZ2Uvc3ZnK3htbDwvZGM6Z" \
  "m9ybWF0PjxkYzp0eXBlIHJkZjpyZXNvdXJjZT0iaHR0cDovL3B1cmwub3JnL2RjL2RjbWl0eXBlL1N0a" \
  "WxsSW1hZ2UiIC8+PGRjOnRpdGxlPjwvZGM6dGl0bGU+PC9jYzpXb3JrPjwvcmRmOlJERj48L21ldGFkY" \
  "XRhPjxnIHRyYW5zZm9ybT0idHJhbnNsYXRlKDAsLTEwMjcuMzYyMikiIGlkPSJsYXllcjEiPjxwYXRoI" \
  "GlkPSJyZWN0NDE1OSIgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoMCwxMDI3LjM2MjIpIiBkPSJNIDAgMCBMI" \
  "DAgMjUgTCAyNSAyNSBMIDI1IDAgTCAwIDAgeiBNIDE3Ljc4OTA2MiAwLjc0NjA5Mzc1IEMgMTguMjk1M" \
  "TM5IDAuNzY3NzkwNDMgMTguNzk2OTM2IDAuOTE1NDM0MzggMTkuMjUgMS4xODU1NDY5IEwgMTkuMjUgM" \
  "jMuODEyNSBDIDE4LjA0MTUxMiAyNC41MzQ2IDE2LjQ4MTgzMiAyNC4zNzc0OTQgMTUuNDQ1MzEyIDIzL" \
  "jMwODU5NCBMIDEwLjM0MTc5NyAxOC4wNDY4NzUgTCA4LjA4Nzg5MDYgMTguMDQ2ODc1IEMgNi43OTk3O" \
  "Tc2IDE4LjA0Njg3NSA1Ljc1IDE2Ljk2MzI2NiA1Ljc1IDE1LjYzNDc2NiBMIDUuNzUgOS4zNjMyODEyI" \
  "EMgNS43NSA4LjAzNDg4MTIgNi43OTk3OTc2IDYuOTUxMTcxOSA4LjA4Nzg5MDYgNi45NTExNzE5IEwgM" \
  "TAuMzQxNzk3IDYuOTUxMTcxOSBMIDE1LjQ0NTMxMiAxLjY4OTQ1MzEgQyAxNi4wOTI3NTkgMS4wMjE3M" \
  "DMxIDE2Ljk0NTYwMiAwLjcwOTkzMjYyIDE3Ljc4OTA2MiAwLjc0NjA5Mzc1IHogIiBzdHlsZT0ib3BhY" \
  "2l0eToxO2ZpbGw6IzAwMDAwMDtmaWxsLW9wYWNpdHk6MTtmaWxsLXJ1bGU6ZXZlbm9kZDtzdHJva2U6b" \
  "m9uZTtzdHJva2Utd2lkdGg6MS41O3N0cm9rZS1taXRlcmxpbWl0OjQ7c3Ryb2tlLWRhc2hhcnJheTpub" \
  "25lO3N0cm9rZS1kYXNob2Zmc2V0OjA7c3Ryb2tlLW9wYWNpdHk6MSIgLz48cGF0aCBpZD0icmVjdDQ1N" \
  "zQiIGQ9Im0gMTkuMjUsMTAyOC41NDczIGMgLTEuMjA4MTcsLTAuNzIwMyAtMi43Njk1OTksLTAuNTY0I" \
  "C0zLjgwNTUxMywwLjUwNDQgbCAtNS4xMDIzNjIsNS4yNjIyIC0yLjI1MzU0MTgsMCBjIC0xLjI4ODA5M" \
  "ywwIC0yLjMzODU4MzIsMS4wODM0IC0yLjMzODU4MzIsMi40MTE4IGwgMCw2LjI3MDkgYyAwLDEuMzI4N" \
  "SAxLjA1MDQ5MDIsMi40MTE5IDIuMzM4NTgzMiwyLjQxMTkgbCAyLjI1MzU0MTgsMCA1LjEwMjM2Miw1L" \
  "jI2MjMgYyAxLjAzNjUyLDEuMDY4OSAyLjU5NzAyNSwxLjIyNjMgMy44MDU1MTMsMC41MDQyIGwgMCwtM" \
  "jIuNjI3NyB6IiBzdHlsZT0iZmlsbDpub25lO2ZpbGwtb3BhY2l0eToxO3N0cm9rZTojZmZmZmZmO3N0c" \
  "m9rZS13aWR0aDoxLjU7c3Ryb2tlLW1pdGVybGltaXQ6NDtzdHJva2UtZGFzaGFycmF5Om5vbmU7c3Ryb" \
  "2tlLW9wYWNpdHk6MSIgLz48L2c+PC9zdmc+\");\n  position: absolute;\n  left: -15px;\n" \
  "  top: 30px;\n  background-color: white;\n}\n.mistplayer .controls .button.sound" \
  " .speaker[data-muted] {\n  background-color: transparent;\n}\n.mistplayer .contr" \
  "ols .button.sound .volume {\n  position: absolute;\n  bottom: 0;\n  left: 1px;\n" \
  "  right: 1px;\n  background-color: white;\n  opacity: 0.6;\n  height: 100%;\n  z" \
  "-index: -1;\n}\n.mistplayer .controls .button.loop {\n  min-height: 45px;\n  bac" \
  "kground-color: transparent;\n  background-image: url(\"data:image/svg+xml;base64" \
  ",PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvbmU9Im5vIj8+PHN2ZyB" \
  "4bWxuczpkYz0iaHR0cDovL3B1cmwub3JnL2RjL2VsZW1lbnRzLzEuMS8iIHhtbG5zOmNjPSJodHRwOi8" \
  "vY3JlYXRpdmVjb21tb25zLm9yZy9ucyMiIHhtbG5zOnJkZj0iaHR0cDovL3d3dy53My5vcmcvMTk5OS8" \
  "wMi8yMi1yZGYtc3ludGF4LW5zIyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI" \
  "geG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB3aWR0aD0iNDUiIGhlaWdodD0iNDUiIGl" \
  "kPSJzdmczOTM3IiB2ZXJzaW9uPSIxLjEiPiA8ZGVmcyBpZD0iZGVmczM5MzkiIC8+IDxtZXRhZGF0YSB" \
  "pZD0ibWV0YWRhdGEzOTQyIj4gPHJkZjpSREY+IDxjYzpXb3JrIHJkZjphYm91dD0iIj4gPGRjOmZvcm1" \
  "hdD5pbWFnZS9zdmcreG1sPC9kYzpmb3JtYXQ+IDxkYzp0eXBlIHJkZjpyZXNvdXJjZT0iaHR0cDovL3B" \
  "1cmwub3JnL2RjL2RjbWl0eXBlL1N0aWxsSW1hZ2UiIC8+IDxkYzp0aXRsZT48L2RjOnRpdGxlPiA8L2N" \
  "jOldvcms+IDwvcmRmOlJERj4gPC9tZXRhZGF0YT4gPHBhdGggaWQ9InJlY3Q0NTExIiBkPSJNIDAgMCB" \
  "MIDAgNDUgTCA0NSA0NSBMIDQ1IDAgTCAwIDAgeiBNIDIyLjUgMTEuMjUgQSAxMS4yNSAxMS4yNSAwIDA" \
  "gMSAzMy43NSAyMi41IEEgMTEuMjUgMTEuMjUgMCAwIDEgMjIuNSAzMy43NSBBIDExLjI1IDExLjI1IDA" \
  "gMCAxIDE0LjU1MDc4MSAzMC40NDkyMTkgTCAxMi43MTQ4NDQgMzIuMjg1MTU2IEwgMTIuNzE0ODQ0IDI" \
  "1Ljc4NTE1NiBMIDE5LjIxNDg0NCAyNS43ODUxNTYgTCAxNy4zNzY5NTMgMjcuNjIzMDQ3IEEgNy4yNSA" \
  "3LjI1IDAgMCAwIDIyLjUgMjkuNzUgQSA3LjI1IDcuMjUgMCAwIDAgMjkuNzUgMjIuNSBBIDcuMjUgNy4" \
  "yNSAwIDAgMCAyMi41IDE1LjI1IEEgNy4yNSA3LjI1IDAgMCAwIDE3LjM3Njk1MyAxNy4zNzY5NTMgTCA" \
  "xNC41NTA3ODEgMTQuNTUwNzgxIEEgMTEuMjUgMTEuMjUgMCAwIDEgMjIuNSAxMS4yNSB6ICIgc3R5bGU" \
  "9Im9wYWNpdHk6MTtmaWxsOiMwMDAwMDA7ZmlsbC1vcGFjaXR5OjE7ZmlsbC1ydWxlOmV2ZW5vZGQ7c3R" \
  "yb2tlOm5vbmU7c3Ryb2tlLXdpZHRoOjEuNTtzdHJva2UtbWl0ZXJsaW1pdDo0O3N0cm9rZS1kYXNoYXJ" \
  "yYXk6bm9uZTtzdHJva2UtZGFzaG9mZnNldDowO3N0cm9rZS1vcGFjaXR5OjEiIC8+IDxwYXRoIGlkPSJ" \
  "wYXRoNDQ5NSIgZD0iTSAyMi41IDExLjI1IEEgMTEuMjUgMTEuMjUgMCAwIDAgMTQuNTUwNzgxIDE0LjU" \
  "1MDc4MSBMIDE3LjM3Njk1MyAxNy4zNzY5NTMgQSA3LjI1IDcuMjUgMCAwIDEgMjIuNSAxNS4yNSBBIDc" \
  "uMjUgNy4yNSAwIDAgMSAyOS43NSAyMi41IEEgNy4yNSA3LjI1IDAgMCAxIDIyLjUgMjkuNzUgQSA3LjI" \
  "1IDcuMjUgMCAwIDEgMTcuMzc2OTUzIDI3LjYyMzA0NyBMIDE5LjIxNDg0NCAyNS43ODUxNTYgTCAxMi4" \
  "3MTQ4NDQgMjUuNzg1MTU2IEwgMTIuNzE0ODQ0IDMyLjI4NTE1NiBMIDE0LjU1MDc4MSAzMC40NDkyMTk" \
  "gQSAxMS4yNSAxMS4yNSAwIDAgMCAyMi41IDMzLjc1IEEgMTEuMjUgMTEuMjUgMCAwIDAgMzMuNzUgMjI" \
  "uNSBBIDExLjI1IDExLjI1IDAgMCAwIDIyLjUgMTEuMjUgeiAiIHN0eWxlPSJvcGFjaXR5OjE7ZmlsbDp" \
  "ub25lO2ZpbGwtb3BhY2l0eTowLjg1ODQ0NzU7ZmlsbC1ydWxlOmV2ZW5vZGQ7c3Ryb2tlOiNmZmZmZmY" \
  "7c3Ryb2tlLXdpZHRoOjEuNTtzdHJva2UtbWl0ZXJsaW1pdDo0O3N0cm9rZS1kYXNoYXJyYXk6bm9uZTt" \
  "zdHJva2UtZGFzaG9mZnNldDowO3N0cm9rZS1vcGFjaXR5OjEiIC8+PC9zdmc+\");\n}\n.mistplaye" \
  "r .controls .button.loop[data-on] {\n  background-color: rgba(255,255,255,0.6);\n" \
  "}\n.mistplayer .controls .button.fullscreen {\n  background-image: url(\"data:im" \
  "age/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiIHN0YW5kYWxvb" \
  "mU9Im5vIj8+PHN2ZyB4bWxuczpkYz0iaHR0cDovL3B1cmwub3JnL2RjL2VsZW1lbnRzLzEuMS8iIHhtb" \
  "G5zOmNjPSJodHRwOi8vY3JlYXRpdmVjb21tb25zLm9yZy9ucyMiIHhtbG5zOnJkZj0iaHR0cDovL3d3d" \
  "y53My5vcmcvMTk5OS8wMi8yMi1yZGYtc3ludGF4LW5zIyIgeG1sbnM6c3ZnPSJodHRwOi8vd3d3LnczL" \
  "m9yZy8yMDAwL3N2ZyIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB2ZXJzaW9uPSIxL" \
  "jEiIGlkPSJzdmczOTM3IiBoZWlnaHQ9IjQ1IiB3aWR0aD0iNDUiPjxkZWZzIGlkPSJkZWZzMzkzOSIgL" \
  "z48bWV0YWRhdGEgaWQ9Im1ldGFkYXRhMzk0MiI+PHJkZjpSREY+PGNjOldvcmsgcmRmOmFib3V0PSIiP" \
  "jxkYzpmb3JtYXQ+aW1hZ2Uvc3ZnK3htbDwvZGM6Zm9ybWF0PjxkYzp0eXBlIHJkZjpyZXNvdXJjZT0ia" \
  "HR0cDovL3B1cmwub3JnL2RjL2RjbWl0eXBlL1N0aWxsSW1hZ2UiIC8+PGRjOnRpdGxlPjwvZGM6dGl0b" \
  "GU+PC9jYzpXb3JrPjwvcmRmOlJERj48L21ldGFkYXRhPjxnIHRyYW5zZm9ybT0idHJhbnNsYXRlKDAsL" \
  "TEwMDcuMzYyMikiIGlkPSJsYXllcjEiPjxnIHRyYW5zZm9ybT0idHJhbnNsYXRlKDAsLTEuMTA5Mzc1K" \
  "SIgaWQ9Imc0NTYzIj48ZyBpZD0iZzQ1NTgiPjxwYXRoIGlkPSJyZWN0Mzk0NSIgdHJhbnNmb3JtPSJ0c" \
  "mFuc2xhdGUoMCwxMDA3LjM2MjIpIiBkPSJNIDUuMTU2MjUsMTAgQyAzLjY5MTM0NjEsMTAgMi41LDExL" \
  "jE5MTM0NiAyLjUsMTIuNjU2MjUgbCAwLDE5LjY4NzUgQyAyLjUsMzMuODA4NjU0IDMuNjkxMzQ2MSwzN" \
  "SA1LjE1NjI1LDM1IGwgMzQuNjg3NSwwIEMgNDEuMzA4NjU0LDM1IDQyLjUsMzMuODA4NjU0IDQyLjUsM" \
  "zIuMzQzNzUgbCAwLC0xOS42ODc1IEMgNDIuNSwxMS4xOTEzNDYgNDEuMzA4NjU0LDEwIDM5Ljg0Mzc1L" \
  "DEwIEwgNS4xNTYyNSwxMCB6IE0gNSwxMi41MzEyNSBsIDM1LDAgMCwyMCAtMzUsMCAwLC0yMCB6IiBzd" \
  "HlsZT0iZmlsbDojZmZmO2ZpbGwtb3BhY2l0eToxO3N0cm9rZTpub25lIiAvPjxyZWN0IHJ5PSIwIiB5P" \
  "SIxMDE5Ljg2MjIiIHg9IjUiIGhlaWdodD0iMjAiIHdpZHRoPSIzNSIgaWQ9InJlY3QzOTQ3IiBzdHlsZ" \
  "T0iZmlsbDojZmZmO2ZpbGwtb3BhY2l0eTowLjM5MjE1Njg2O3N0cm9rZTpub25lIiAvPjxwYXRoIGlkP" \
  "SJwYXRoMzk0OSIgdHJhbnNmb3JtPSJ0cmFuc2xhdGUoMCwxMDA3LjM2MjIpIiBkPSJtIDE4Ljc4MTI1L" \
  "DM1LjQwNjI1IGMgLTEuNTM2NjEsMC4zNzk4MDkgLTIuOTcxNDY1LDAuOTkxNTU3IC00LjI4MTI1LDEuO" \
  "DEyNSBsIDE1LjY1NjI1LDAgYyAtMS4zMTMwMDUsLTAuODIyOTYxIC0yLjc2MjgyNSwtMS40MzI5NTMgL" \
  "TQuMzEyNSwtMS44MTI1IGwgLTcuMDYyNSwwIHoiIHN0eWxlPSJmaWxsOiNmZmY7ZmlsbC1vcGFjaXR5O" \
  "jE7c3Ryb2tlOm5vbmUiIC8+PC9nPjxnIGlkPSJnNDAwNyIgdHJhbnNmb3JtPSJtYXRyaXgoMi4wMzUzO" \
  "Tg1LDAsMCwxLjE2MzA4MjgsLTk5LjMyMTczNCwtMTQxLjU0NTgxKSIgc3R5bGU9ImZpbGw6IzAwMCI+P" \
  "HBhdGggaWQ9InJlY3QzOTU4IiBkPSJtIDY1LjUzMzY0NiwxMDAxLjQ3NTggLTIuMDMyOTMyLDAgMC42N" \
  "jI5MTMsMC42NjI5IC0yLjI1MzkwMywyLjI1MzkgMC43MDcxMDcsMC43MDcxIDIuMjUzOTAzLC0yLjI1M" \
  "zkgMC42NjI5MTIsMC42NjI5IDAsLTIuMDMyOSB6IiBzdHlsZT0iZmlsbDojZmZmO2ZpbGwtb3BhY2l0e" \
  "ToxO3N0cm9rZTpub25lIiAvPjxwYXRoIGlkPSJyZWN0Mzk1OC01IiBkPSJtIDY1LjUzMzY0NiwxMDEyL" \
  "jg0IDAsLTIuMDMzIC0wLjY2MjgzNiwwLjY2MjkgLTIuMjUzOTAxLC0yLjI1MzkgLTAuNzA3MTA0LDAuN" \
  "zA3MSAyLjI1MzkwMiwyLjI1MzkgLTAuNjYyOTA2LDAuNjYyOSAyLjAzMjg0NSwxZS00IHoiIHN0eWxlP" \
  "SJmaWxsOiNmZmY7ZmlsbC1vcGFjaXR5OjE7c3Ryb2tlOm5vbmUiIC8+PHBhdGggaWQ9InJlY3QzOTU4L" \
  "TUxIiBkPSJtIDU0LjE2OTQzLDEwMDEuNDc1OCAyLjAzMjkzMiwwIC0wLjY2MjkxMywwLjY2MjkgMi4yN" \
  "TM5MDMsMi4yNTM5IC0wLjcwNzEwNywwLjcwNzEgLTIuMjUzOTAzLC0yLjI1MzkgLTAuNjYyOTEyLDAuN" \
  "jYyOSAwLC0yLjAzMjkgeiIgc3R5bGU9ImZpbGw6I2ZmZjtmaWxsLW9wYWNpdHk6MTtzdHJva2U6bm9uZ" \
  "SIgLz48cGF0aCBpZD0icmVjdDM5NTgtNS03IiBkPSJtIDU0LjE2OTQzLDEwMTIuODQgMCwtMi4wMzMgM" \
  "C42NjI4MzYsMC42NjI5IDIuMjUzOTAxLC0yLjI1MzkgMC43MDcxMDQsMC43MDcxIC0yLjI1MzkwMiwyL" \
  "jI1MzkgMC42NjI5MDYsMC42NjI5IC0yLjAzMjg0NSwxZS00IHoiIHN0eWxlPSJmaWxsOiNmZmY7Zmlsb" \
  "C1vcGFjaXR5OjE7c3Ryb2tlOm5vbmUiIC8+PC9nPjwvZz48L2c+PC9zdmc+\");\n  height: 45px;" \
  "\n}\n.mistplayer .controls .button.tracks {\n  line-height: 25px;\n  width: 100%" \
  ";\n  margin: 0;\n  height: 25px;\n  padding: 0 15px;\n  box-sizing: border-box;\n" \
  "  -webkit-user-select: none;\n  -moz-user-select: none;\n  -ms-user-select: none" \
  ";\n  user-select: none;\n}\n.mistplayer .controls .tracks .settings {\n  positio" \
  "n: absolute;\n  background-color: black;\n  padding: 5px 10px;\n  right: -1000px" \
  ";\n  bottom: 27px;\n}\n.mistplayer .controls .tracks:hover .settings {\n  right:" \
  " 0;\n}\n.mistplayer .controls .tracks:not(:hover) .settings {\n  transition: rig" \
  "ht 0.5s ease-in 1s;\n}\n.mistplayer .controls .tracks .settings label {\n  text-" \
  "align: left;\n  display: flex;\n  flex-flow: row nowrap;\n}\n.mistplayer .contro" \
  "ls .tracks .settings label > *:not(:first-child) {\n  margin-left: 1em;\n  flex-" \
  "grow: 1;\n}\n.mistplayer .controls .tracks .settings label span {\n  text-transf" \
  "orm: capitalize;\n  -webkit-user-select: none;\n  -moz-user-select: none;\n  -ms" \
  "-user-select: none;\n  user-select: none;\n}\n.mistplayer .controls .tracks .set" \
  "tings label select {\n  background: none;\n  color: white;\n  border: none;\n  o" \
  "utline: none;\n}\n.mistplayer .controls .tracks .settings label option {\n  colo" \
  "r: black;\n}\n\n@keyframes spin {\n  0% {\n    transform: rotate(0deg);\n  }\n  " \
  "100% {\n    transform: rotate(360deg);\n  }\n}\n";
unsigned int mist_css_len = 26810;
