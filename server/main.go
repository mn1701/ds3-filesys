// Package main exposes a bounded local HTTP adapter for the DS3 image CLI.
package main

import (
 "context"
 "crypto/subtle"
 "encoding/json"
 "errors"
 "io"
 "log"
 "net/http"
 "os"
 "os/exec"
 "path/filepath"
 "strings"
 "time"
)

type gateway struct { binary, image, token string }
func (g gateway) run(ctx context.Context, command string, args ...string) ([]byte,error) {
 ctx,cancel:=context.WithTimeout(ctx,10*time.Second); defer cancel()
 params:=append([]string{command,g.image},args...)
 cmd:=exec.CommandContext(ctx,g.binary,params...)
 return cmd.Output()
}
func (g gateway) serve(w http.ResponseWriter,r *http.Request) {
 w.Header().Set("Cache-Control","no-store")
 if subtle.ConstantTimeCompare([]byte(r.Header.Get("Authorization")),[]byte("Bearer "+g.token))!=1 {
  w.Header().Set("WWW-Authenticate","Bearer"); http.Error(w,"unauthorized",401); return
 }
 imagePath:=r.URL.Query().Get("path")
 if imagePath=="" { imagePath="/" }
 if !strings.HasPrefix(imagePath,"/") || len(imagePath)>4096 { http.Error(w,"invalid image path",400); return }
 var out []byte; var err error
 switch {
 case r.URL.Path=="/health" && r.Method=="GET":
  out,err=g.run(r.Context(),"info")
 case r.URL.Path=="/entries" && r.Method=="GET":
  out,err=g.run(r.Context(),"ls",imagePath)
 case r.URL.Path=="/directories" && r.Method=="POST":
  out,err=g.run(r.Context(),"mkdir",imagePath)
 case r.URL.Path=="/files" && r.Method=="GET":
  out,err=g.run(r.Context(),"cat",imagePath)
 case r.URL.Path=="/files" && r.Method=="DELETE":
  out,err=g.run(r.Context(),"rm",imagePath)
 case r.URL.Path=="/files" && r.Method=="PUT":
  body:=http.MaxBytesReader(w,r.Body,128*1024)
  temp,createErr:=os.CreateTemp("","ds3-upload-*")
  if createErr!=nil { http.Error(w,"upload unavailable",503); return }
  defer os.Remove(temp.Name())
  _,copyErr:=io.Copy(temp,body); closeErr:=temp.Close()
  if copyErr!=nil {
   var max *http.MaxBytesError
   if errors.As(copyErr,&max) { http.Error(w,"maximum file size is 128 KiB",413) } else { http.Error(w,"upload failed",400) }; return
  }
  if closeErr!=nil { http.Error(w,"upload failed",503); return }
  out,err=g.run(r.Context(),"put",imagePath,temp.Name())
 default: http.Error(w,"unknown route or method",404); return
 }
 if err!=nil {
  if errors.Is(r.Context().Err(),context.Canceled) { return }
  w.Header().Set("Content-Type","application/json")
  w.WriteHeader(409)
  // CLI stderr stays on the server; it can contain host paths.
  _=json.NewEncoder(w).Encode(map[string]string{"error":"image operation failed"})
  log.Printf("%s %s failed: %v",r.Method,r.URL.Path,err); return
 }
 w.Header().Set("Content-Type","application/octet-stream")
 if len(out)==0 { w.WriteHeader(204); return }
 _,_=w.Write(out)
}
func main() {
 token:=os.Getenv("DS3_TOKEN")
 if len(token)<32 { log.Fatal("DS3_TOKEN requires at least 32 characters") }
 binary:=os.Getenv("DS3_BINARY"); if binary=="" { binary="../build/ds3" }
 image:=os.Getenv("DS3_IMAGE"); if image=="" { image="../workspace.ds3" }
 binary,err:=filepath.Abs(binary); if err!=nil { log.Fatal(err) }
 image,err=filepath.Abs(image); if err!=nil { log.Fatal(err) }
 g:=gateway{binary:binary,image:image,token:token}
 // Limit concurrent subprocesses, including waiters on the image lock.
 slots:=make(chan struct{},8)
 handler:=http.HandlerFunc(func(w http.ResponseWriter,r *http.Request) {
  select { case slots<-struct{}{}: defer func(){<-slots}(); g.serve(w,r)
  default: http.Error(w,"busy",503) }
 })
 server:=&http.Server{Addr:"127.0.0.1:8088",Handler:handler,ReadHeaderTimeout:5*time.Second,ReadTimeout:15*time.Second,WriteTimeout:20*time.Second,IdleTimeout:30*time.Second,MaxHeaderBytes:8192}
 log.Print("DS3 gateway listening on 127.0.0.1:8088")
 log.Fatal(server.ListenAndServe())
}
