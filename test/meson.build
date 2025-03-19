
# Testing binaries that are not unit tests, but intended for manual use
convertertest = executable('convertertest', 'converter.cpp', header_tgts, dependencies: libmist_dep)
urireadertest = executable('urireadertest', 'urireader.cpp', header_tgts, dependencies: libmist_dep)
resolvetest = executable('resolvetest', 'resolve.cpp', header_tgts, dependencies: libmist_dep)
streamstatustest = executable('streamstatustest', 'status.cpp', header_tgts, dependencies: libmist_dep)
websockettest = executable('websockettest', 'websocket.cpp', header_tgts, dependencies: libmist_dep)

# Actual unit tests
test('Redirecting log messages produces no error', exec_tgts.get('MistUtilLog'), suite:'Logs', args: ['BadBinary'], should_fail: true)

downloadertest = executable('downloadertest', 'downloader.cpp', header_tgts, dependencies: libmist_dep)
test('event-looped non-blocking', downloadertest, args: ['AUTO'], env: {'EVENTLOOP':'1'}, suite: 'Downloader')
test('blocking', downloadertest, args: ['AUTO'], suite: 'Downloader')


# The only argument is simply the 'alg' parameter value as defined in the JWA spec (RFC7518)
jwttest = executable('jwttest', 'jwt.cpp', header_tgts, dependencies: libmist_dep)
test('No digital signature or MAC performed', jwttest, suite: 'JSON Web Tokens', args: ['none','eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0'])
test('HMAC using SHA-256', jwttest, suite: 'JSON Web Tokens', args: ['HS256','eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.KMUFsIDTnFmyG3nMiGM6H9FNFUROf3wh7SmqJp-QV30','{"kty":"oct","k":"YS1zdHJpbmctc2VjcmV0LWF0LWxlYXN0LTI1Ni1iaXRzLWxvbmc="}'])
test('HMAC using SHA-384', jwttest, suite: 'JSON Web Tokens', args: ['HS384','eyJhbGciOiJIUzM4NCIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.owv7q9nVbW5tqUezF_G2nHTra-ANW3HqW9epyVwh08Y-Z-FKsnG8eBIpC4GTfTVU','{"kty":"oct","k":"YS12YWxpZC1zdHJpbmctc2VjcmV0LXRoYXQtaXMtYXQtbGVhc3QtMzg0LWJpdHMtbG9uZw=="}'])
test('HMAC using SHA-512', jwttest, suite: 'JSON Web Tokens', args: ['HS512','eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.ANCf_8p1AE4ZQs7QuqGAyyfTEgYrKSjKWkhBk5cIn1_2QVr2jEjmM-1tu7EgnyOf_fAsvdFXva8Sv05iTGzETg','{"kty":"oct","k":"YS12YWxpZC1zdHJpbmctc2VjcmV0LXRoYXQtaXMtYXQtbGVhc3QtNTEyLWJpdHMtbG9uZy13aGljaC1pcy12ZXJ5LWxvbmc="}'])
test('RSASSA-PKCS1-v1_5 using SHA-256', jwttest, suite: 'JSON Web Tokens', args: ['RS256','eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.NHVaYe26MbtOYhSKkoKYdFVomg4i8ZJd8_-RU8VNbftc4TSMb4bXP3l3YlNWACwyXPGffz5aXHc6lty1Y2t4SWRqGteragsVdZufDn5BlnJl9pdR_kdVFUsra2rWKEofkZeIC4yWytE58sMIihvo9H1ScmmVwBcQP6XETqYd0aSHp1gOa9RdUPDvoXQ5oqygTqVtxaDr6wUFKrKItgBMzWIdNZ6y7O9E0DhEPTbE9rfBo6KTFsHAZnMg4k68CDp2woYIaXbmYTWcvbzIuHO7_37GT79XdIwkm95QJ7hYC9RiwrV7mesbY4PAahERJawntho0my942XheVLmGwLMBkQ','{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB"}'])
test('RSASSA-PKCS1-v1_5 using SHA-384', jwttest, suite: 'JSON Web Tokens', args: ['RS384','eyJhbGciOiJSUzM4NCIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.o1hC1xYbJolSyh0-bOY230w22zEQSk5TiBfc-OCvtpI2JtYlW-23-8B48NpATozzMHn0j3rE0xVUldxShzy0xeJ7vYAccVXu2Gs9rnTVqouc-UZu_wJHkZiKBL67j8_61L6SXswzPAQu4kVDwAefGf5hyYBUM-80vYZwWPEpLI8K4yCBsF6I9N1yQaZAJmkMp_Iw371Menae4Mp4JusvBJS-s6LrmG2QbiZaFaxVJiW8KlUkWyUCns8-qFl5OMeYlgGFsyvvSHvXCzQrsEXqyCdS4tQJd73ayYA4SPtCb9clz76N1zE5WsV4Z0BYrxeb77oA7jJhh994RAPzCG0hmQ','{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB"}'])
test('RSASSA-PKCS1-v1_5 using SHA-512', jwttest, suite: 'JSON Web Tokens', args: ['RS512','eyJhbGciOiJSUzUxMiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.jYW04zLDHfR1v7xdrW3lCGZrMIsVe0vWCfVkN2DRns2c3MN-mcp_-RE6TN9umSBYoNV-mnb31wFf8iun3fB6aDS6m_OXAiURVEKrPFNGlR38JSHUtsFzqTOj-wFrJZN4RwvZnNGSMvK3wzzUriZqmiNLsG8lktlEn6KA4kYVaM61_NpmPHWAjGExWv7cjHYupcjMSmR8uMTwN5UuAwgW6FRstCJEfoxwb0WKiyoaSlDuIiHZJ0cyGhhEmmAPiCwtPAwGeaL1yZMcp0p82cpTQ5Qb-7CtRov3N4DcOHgWYk6LomPR5j5cCkePAz87duqyzSMpCB0mCOuE3CU2VMtGeQ','{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB"}'])
test('ECDSA using P-256 and SHA-256', jwttest, suite: 'JSON Web Tokens', args: ['ES256','eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.tyh-VfuzIxCyGYDlkBA7DfyjrqmSHu6pQ2hoZuFqUSLPNY2N0mpHb3nk5K17HWP_3cYHBw7AhHale5wky6-sVA','{"kty":"EC","use":"sig","alg":"ES256","crv":"P-256","x":"EVs_o5-uQbTjL3chynL4wXgUg2R9q9UU8I5mEovUf84","y":"kGe5DgSIycKp8w9aJmoHhB1sB3QTugfnRWm5nU_TzsY"}'])
test('ECDSA using P-384 and SHA-384', jwttest, suite: 'JSON Web Tokens', args: ['ES384','eyJhbGciOiJFUzM4NCIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.VUPWQZuClnkFbaEKCsPy7CZVMh5wxbCSpaAWFLpnTe9J0--PzHNeTFNXCrVHysAa3eFbuzD8_bLSsgTKC8SzHxRVSj5eN86vBPo_1fNfE7SHTYhWowjY4E_wuiC13yoj','{"kty":"EC","use":"sig","alg":"ES384","crv":"P-384","x":"C1uWSXj2czCDwMTLWV5BFmwxdM6PX9p-Pk9Yf9rIf374m5XP1U8q79dBhLSIuaoj","y":"svOT39UUcPJROSD1FqYLued0rXiooIii1D3jaW6pmGVJFhodzC31cy5sfOYotrzF"}'])
test('ECDSA using P-521 and SHA-512', jwttest, suite: 'JSON Web Tokens', args: ['ES512','eyJhbGciOiJFUzUxMiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.AbVUinMiT3J_03je8WTOIl-VdggzvoFgnOsdouAs-DLOtQzau9valrq-S6pETyi9Q18HH-EuwX49Q7m3KC0GuNBJAc9Tksulgsdq8GqwIqZqDKmG7hNmDzaQG1Dpdezn2qzv-otf3ZZe-qNOXUMRImGekfQFIuH_MjD2e8RZyww6lbZk','{"kty":"EC","use":"sig","alg":"ES512","crv":"P-521","x":"AYHOB2c_v3wWwu5ZhMMNADtzSvcFWTw2dFRJ7GlBSxGKU82_dJyE7SVHD1G7zrHWSGdUPH526rgGIMVy-VIBzKMs","y":"AIm-O-jJMsmID5NAV2at5quMyJk0TbmlPbJY_U1Sd0nE9y9W18FbjyQ86a-RjhaWo_lsDAJfBuwqsKCTrFuynXZ7"}'])
test('RSASSA-PSS using SHA-256 and MGF1 with SHA-256', jwttest, suite: 'JSON Web Tokens', args: ['PS256','eyJhbGciOiJQUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.iOeNU4dAFFeBwNj6qdhdvm-IvDQrTa6R22lQVJVuWJxorJfeQww5Nwsra0PjaOYhAMj9jNMO5YLmud8U7iQ5gJK2zYyepeSuXhfSi8yjFZfRiSkelqSkU19I-Ja8aQBDbqXf2SAWA8mHF8VS3F08rgEaLCyv98fLLH4vSvsJGf6ueZSLKDVXz24rZRXGWtYYk_OYYTVgR1cg0BLCsuCvqZvHleImJKiWmtS0-CymMO4MMjCy_FIl6I56NqLE9C87tUVpo1mT-kbg5cHDD8I7MjCW5Iii5dethB4Vid3mZ6emKjVYgXrtkOQ-JyGMh6fnQxEFN1ft33GX2eRHluK9eg','{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB"}'])
test('RSASSA-PSS using SHA-384 and MGF1 with SHA-384', jwttest, suite: 'JSON Web Tokens', args: ['PS384','eyJhbGciOiJQUzM4NCIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.Lfe_aCQme_gQpUk9-6l9qesu0QYZtfdzfy08w8uqqPH_gnw-IVyQwyGLBHPFBJHMbifdSMxPjJjkCD0laIclhnBhowILu6k66_5Y2z78GHg8YjKocAvB-wSUiBhuV6hXVxE5emSjhfVz2OwiCk2bfk2hziRpkdMvfcITkCx9dmxHU6qcEIsTTHuH020UcGayB1-IoimnjTdCsV1y4CMr_ECDjBrqMdnontkqKRIM1dtmgYFsJM6xm7ewi_ksG_qZHhaoBkxQ9wq9OVQRGiSZYowCp73d2BF3jYMhdmv2JiaUz5jRvv6lVU7Quq6ylVAlSPxeov9voYHO1mgZFCY1kQ','{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB"}'])
test('RSASSA-PSS using SHA-512 and MGF1 with SHA-512', jwttest, suite: 'JSON Web Tokens', args: ['PS512','eyJhbGciOiJQUzUxMiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlhdCI6MTUxNjIzOTAyMn0.J5W09-rNx0pt5_HBiydR-vOluS6oD-RpYNa8PVWwMcBDQSXiw6-EPW8iSsalXPspGj3ouQjAnOP_4-zrlUUlvUIt2T79XyNeiKuooyIFvka3Y5NnGiOUBHWvWcWp4RcQFMBrZkHtJM23sB5D7Wxjx0-HFeNk-Y3UJgeJVhg5NaWXypLkC4y0ADrUBfGAxhvGdRdULZivfvzuVtv6AzW6NRuEE6DM9xpoWX_4here-yvLS2YPiBTZ8xbB3axdM99LhES-n52lVkiX5AWg2JJkEROZzLMpaacA_xlbUz_zbIaOaoqk8gB5oO7kI6sZej3QAdGigQy-hXiRnW_L98d4GQ','{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB"}'])


sockbuftest = executable('sockbuftest', 'socketbuffer.cpp', header_tgts, dependencies: libmist_dep)
test('Socket buffer test 8KiB', sockbuftest, args: ['1024'])
test('Socket buffer test 64KiB', sockbuftest, args: ['8192'])
test('Socket buffer test 8MiB', sockbuftest, args: ['1048576'])

proctest = executable('proctest', 'procs.cpp', header_tgts, dependencies: libmist_dep)
test('Retrieve stdout from child', proctest, suite: 'Procs', args: ['output_capture'])
test('Child moves stdin to stdout ', proctest, suite: 'Procs', args: ['output_loop'])
test('Sleep then retrieve stdout', proctest, suite: 'Procs', args: ['output_delay'])
test('Check ability to kill lingering child processes', proctest, suite: 'Procs', args: ['child_linger'])
test('Check piped process with good/bad file descriptors', proctest, suite: 'Procs', args: ['fds'])

shellsplittest = executable('shellsplittest', 'shellsplit.cpp', header_tgts, dependencies: libmist_dep)
test('Shell argument splitter', shellsplittest, args: ['hi "a banana" "" sauce    pudding "miauw"\'"\' "a"\'\'"z"'], env: {'OUTC':'7', 'OUT1':'hi', 'OUT2':'a banana', 'OUT3':'', 'OUT4':'sauce', 'OUT5':'pudding', 'OUT6':'miauw"', 'OUT7':'az'})

jsontest = executable('jsontest', 'json.cpp', header_tgts, dependencies: libmist_dep)
test('Empty array', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'[]', 'JSON_RESULT':'[]'})
test('Empty object', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'{}', 'JSON_RESULT':'{}'})
test('Blank string', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'', 'JSON_RESULT':''})
test('Newline', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'\n', 'JSON_RESULT':''})
test('Carriage return, newline', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'\r\n', 'JSON_RESULT':''})
test('Array with carriage return and newline mixed', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'\r\n[\n1\n,\n2\r\n]', 'JSON_RESULT':'[1,2]'})
test('Large integer', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'123456789000', 'JSON_RESULT':'123456789000'})
test('11 slices of Pi', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'3.1415926536', 'JSON_RESULT':'3.1415926536'})
test('String with unicode', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'"(\u256F\u00B0\u25A1\u00B0\uFF09\u256F\uFE35 \u253B\u2501\u253B"', 'JSON_RESULT':'"(\\u256F\\u00B0\\u25A1\\u00B0\\uFF09\\u256F\\uFE35 \\u253B\\u2501\\u253B"'})
test('Array of various bools and nulls', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'[n,n,t,t,f,n,f,n,n]', 'JSON_RESULT':'[null,null,true,true,false,null,false,null,null]'})
test('Unclosed array', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'[[]', 'JSON_RESULT':'[[]]'})
test('Invalid symbols', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'-inf', 'JSON_RESULT':'null'})
test('Invalid symbols in array', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'[-inf]', 'JSON_RESULT':'[null]'})
test('Complex structure', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'[{"a":[1,2,3.14]},n,[t,f]]', 'JSON_RESULT':'[{"a":[1,2,3.14]},null,[true,false]]'})
test('Empty object member', jsontest, suite: 'JSON parser / printer', env : {'JSON_STRING':'{"test":}', 'JSON_RESULT':'{"test":null}'})

urltest = executable('urltest', 'url.cpp', header_tgts, dependencies: libmist_dep)
urltest_vm = {'T_PATH':'', 'T_QUERY':'', 'T_FRAG':'', 'T_USER':'', 'T_PASS':'', 'T_NORM':'', 'T_EXT':''}
urltest_v = urltest_vm + {'T_PROTO':'', 'T_HOST':'', 'T_PORT':'0'}
test('HTTPS URL with parent directory', urltest, suite: 'URL parser',
      env: urltest_v + {'T_PROTO':'https', 'T_HOST':'google.com', 'T_PORT':'443', 'T_PATH':'directory/', 'T_NORM':'https://google.com/directory/'},
      args: ['https://google.com/directory/subdirectory/..'])
test('Partially cancelled out path', urltest, suite: 'URL parser',
      env: urltest_v + {'T_PROTO':'http', 'T_HOST':'root', 'T_PORT':'80', 'T_PATH':'home/mo', 'T_NORM':'http://root/home/mo'},
      args: ['http://root/home/schmerkel/../mo'])
urltest_relpath = urltest_v + {'T_PROTO':'https', 'T_HOST':'relpath.com', 'T_PORT':'443', 'T_NORM':'https://relpath.com/'}
test('Fully cancelled out path', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/rel/lang/../..'])
test('Current directory reference stripping', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/rel/./..'])
test('Empty directory stripping', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/////'])
test('Numerical path deletion', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/123/..'])
test('Leading single char path deletion', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/a/..'])
test('Trailing URL current dir path deletion', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/.'])
test('Trailing file current dir path deletion', urltest, suite: 'URL parser',
      env: urltest_v + {'T_PATH':'bla/', 'T_NORM':'/bla/'},
      args: ['///bla/.'])
test('Extension', urltest, suite: 'URL parser',
      env: urltest_v + {'T_EXT':'mp4', 'T_PATH':'test.mp4', 'T_NORM':'/test.mp4'},
      args: ['///test.mp4'])
test('Parent of root directory', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/rel/../..'])
test('All URL components present', urltest, suite: 'URL parser',
      env: {'T_PROTO':'prot', 'T_HOST':'a.bc', 'T_PORT':'3000', 'T_PATH':'path/to/some/file.cpp', 'T_QUERY':'bool=true&int=3', 'T_FRAG':'frag', 'T_USER':'uname', 'T_PASS':'pwd', 'T_NORM':'prot://uname:pwd@a.bc:3000/path/to/some/file.cpp?bool=true&int=3#frag'},
      args: ['prot://uname:pwd@a.bc:3000/path/to/err/../some/file.cpp?bool=true&int=3#frag'])
test('Unknown port for custom protocol', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'path/', 'T_NORM':'prot://authority.nl/path/'},
      args : ['prot://authority.nl/path/weird/./..'])
test('Query/fragment chars before ? char (escaped)', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'p&a=t#h/', 'T_NORM':'prot://authority.nl/p%26a=t%23h/'},
      args : ['prot://authority.nl/p%26a=t%23h/'])
test('Query/fragment chars before ? char (unescaped)', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'p&a=t', 'T_FRAG':'h/', 'T_NORM':'prot://authority.nl/p%26a=t#h/'},
      args : ['prot://authority.nl/p&a=t#h/'])
test('Path with spaces', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'p at!h/', 'T_NORM':'prot://authority.nl/p%20at!h/'},
      args : ['prot://authority.nl/p%20at%21h/'])
test('Escaped characters', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':' !"#$%&()/*+,-.//0123456789/:;<=>?@/', 'T_NORM':'prot://authority.nl/%20!%22%23$%25%26()/*%2b,-.//0123456789/:;%3c=%3e%3f@/'},
      args : ['prot://authority.nl/%20%21%22%23%24%25%26%28%29/%2A%2B%2C%2D%2E%2F/%30%31%32%33%34%35%36%37%38%39/%3A%3B%3C%3D%3E%3F%40/'])
test('Linking / Absolute', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'sub1/sub2/', 'T_NORM':'/sub1/sub2/'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/'])
test('Linking / Relative', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'sub1/sub2/www.wiki.com/anders', 'T_NORM':'/sub1/sub2/www.wiki.com/anders'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/', 'www.wiki.com/anders'])
test('Linking / Relative with current and parent dir', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'sub1/sub2/relatief/', 'T_NORM':'/sub1/sub2/relatief/'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/', 'www.wiki.com/anders', './../relatief/'])
test('Linking / Relative file in absolute directory', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'absoluut/relatiefFile', 'T_NORM':'/absoluut/relatiefFile'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/', 'www.wiki.com/anders', './../relatief/', '/absoluut/', 'relatiefFile'])
test('Linking / Protocol switch', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'www.wiki.com', 'T_PORT':'80', 'T_NORM':'http://www.wiki.com/'},
      args : ['file://google.com/doe/iets', '/absoluut/', 'relatiefFile', 'http://www.wiki.com'])
test('Linking / Protocol absolute', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'example.com', 'T_PORT':'80', 'T_NORM':'http://example.com/'},
      args : ['http://www.wiki.com', '//example.com'])
test('UTF-8 emoji in path', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'😋😄😡😵/', 'T_NORM':'prot://authority.nl/%f0%9f%98%8b%f0%9f%98%84%f0%9f%98%a1%f0%9f%98%b5/'},
      args : ['prot://authority.nl/😋😄😡😵/'])
test('UTF-8 emoji in file', urltest, suite: 'URL parser',
      env : urltest_v + {'T_HOST':'', 'T_PATH':'🐇.ts', 'T_NORM':'/🐇.ts', 'T_EXT':'ts'},
      args : ['/🐇.ts'])
test('UTF-8 reverse char in path', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'a.nl', 'T_PATH':'reverse‏reverse/', 'T_NORM':'prot://a.nl/reverse%e2%80%8freverse/'},
      args : ['prot://a.nl/reverse%E2%80%8Freverse/'])
test('IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::1', 'T_PORT':'159', 'T_NORM':'https://[2001:db8::1]:159/'},
      args : ['https://[2001:db8::1]:159/'])
test('IPv6 address with non-numeric port', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::aa', 'T_PORT':'443', 'T_NORM':'https://[2001:db8::aa]/'},
      args : ['https://[2001:db8:]:aa/'])
test('Bare IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::1', 'T_PORT':'159', 'T_NORM':'https://[2001:db8::1]:159/'},
      args : ['https://2001:db8::1:159/'])
test('Bare hexadecimal IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::aa', 'T_PORT':'443', 'T_NORM':'https://[2001:db8::aa]/'},
      args : ['https://2001:db8::aa/'])
test('Username but no password', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'b', 'T_PORT':'80', 'T_USER':'a', 'T_NORM':'http://a@b/'},
      args : ['http://a@b'])
test('Username and password linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'c', 'T_PORT':'80', 'T_PASS':'b', 'T_USER':'a', 'T_NORM':'http://a:b@c/test', 'T_PATH':'test'},
      args : ['http://a:b@c', 'test'])
test('Username but no password linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'c', 'T_PORT':'80', 'T_USER':'a', 'T_NORM':'http://a@c/test', 'T_PATH':'test'},
      args : ['http://a@c', 'test'])
test('Protocol-absolute base', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'', 'T_HOST':'test', 'T_NORM':'//test/'},
      args : ['--direct', '//test'])
test('Bare port number', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PORT':'42', 'T_NORM':'//:42/'},
      args : ['--direct', '42'])
test('Bare port number linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PORT':'42', 'T_NORM':'//:42/test', 'T_PATH':'test'},
      args : ['--direct', '42', 'test'])
test('Query string without path', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'', 'T_HOST':'test', 'T_QUERY':'test=test', 'T_NORM':'//test/?test=test'},
      args : ['--direct', '//test?test=test'])
test('RTMP IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtmp', 'T_HOST':'2001:db8::1', 'T_PORT':'1935', 'T_NORM':'rtmp://[2001:db8::1]/'},
      args : ['rtmp://[2001:db8::1]/'])
test('RTMPS', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtmps', 'T_HOST':'2001:db8::1', 'T_PORT':'443', 'T_NORM':'rtmps://[2001:db8::1]/'},
      args : ['rtmps://[2001:db8::1]/'])
test('DTSC', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'dtsc', 'T_HOST':'2001:db8::1', 'T_PORT':'4200', 'T_NORM':'dtsc://[2001:db8::1]/'},
      args : ['dtsc://[2001:db8::1]/'])
test('RTSP', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtsp', 'T_HOST':'2001:db8::1', 'T_PORT':'554', 'T_NORM':'rtsp://[2001:db8::1]/'},
      args : ['rtsp://[2001:db8::1]/'])
test('RTSP linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtsp', 'T_HOST':'2001:db8::1', 'T_PORT':'554', 'T_NORM':'rtsp://[2001:db8::1]/test', 'T_PATH':'test'},
      args : ['rtsp://[2001:db8::1]/', 'test'])



dtsc_sizing_test = executable('dtsc_sizing_test', 'dtsc_sizing.cpp', header_tgts, dependencies: libmist_dep)
test('DTSC Sizing Test', dtsc_sizing_test)

bitwritertest = executable('bitwritertest', 'bitwriter.cpp', header_tgts, dependencies: libmist_dep)
test('bitWriter Test', bitwritertest)

httpparsertest = executable('httpparsertest', 'http_parser.cpp', header_tgts, dependencies: libmist_dep)
test('GET request for /', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'GET / HTTP/1.1\n\n', 'T_COUNT':'1'})
test('GET request for / with carriage returns', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'GET / HTTP/1.1\r\n\r\n', 'T_COUNT':'1'})
test('POST request to /, raw body', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'POST / HTTP/1.1\nContent-Length: 4\nContent-Type: text/plain\n\ntest', 'T_COUNT':'1'})
test('POST request to /, urlencoded body', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'POST / HTTP/1.1\nContent-Length: 28\nContent-Type: application/x-www-form-urlencoded\n\nfoo=bar&banana=sauce&cookies', 'T_COUNT':'1'})
test('Blank HTTP response, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 0\n\n', 'T_COUNT':'1'})
test('Blank HTTP response, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 0\n\n', 'T_LINGER':'1', 'T_COUNT':'1'})
test('Simple HTTP response, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 4\n\ntest', 'T_COUNT':'1'})
test('Simple HTTP response, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 4\n\ntest', 'T_LINGER':'1', 'T_COUNT':'1'})
test('Simple HTTP response, no length, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\n\ntest', 'T_COUNT':'1'})
test('Simple HTTP response, no length, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\n\ntest', 'T_LINGER':'1', 'T_COUNT':'0'})
test('Chunked HTTP response, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nTransfer-Encoding: chunked\n\n1\nt\n3\nest\n0\n\n', 'T_COUNT':'1'})
test('Chunked HTTP response, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nTransfer-Encoding: chunked\n\n1\nt\n3\nest\n0\n\n', 'T_LINGER':'1', 'T_COUNT':'1'})

amftest = executable('amftest', 'amf.cpp', header_tgts, dependencies: libmist_dep)
test('AMF parser', amftest, suite: 'AMF parser', protocol:'tap')

#abst_test = executable('abst_test', 'abst_test.cpp', dependencies: libmist_dep)
#test('MP4::ABST Test', abst_test)

