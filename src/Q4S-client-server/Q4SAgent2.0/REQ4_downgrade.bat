call winhttpjs.bat "http://212.170.54.119:8280/policyserver-webcontroller/sessions/downgrade"  -method POST   -header token.txt -saveTo response.file -body-file alert.json
