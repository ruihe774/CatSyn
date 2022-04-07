#![feature(maybe_uninit_slice)]
#![feature(write_all_vectored)]

use std::env;
use std::io::{IoSlice, Read, Write};
use std::mem::MaybeUninit;
use std::process::{Command, Stdio};
use std::sync::mpsc::sync_channel;
use std::thread;

fn main() {
    let args: Vec<_> = env::args_os().skip(1).collect();
    let sep = args
        .iter()
        .position(|arg| arg == "---")
        .expect("no '---' found in commandline");
    let mut p1 = Command::new(args[0].as_os_str())
        .args(args.iter().skip(1).take(sep - 1))
        .stdout(Stdio::piped())
        .spawn()
        .expect("process 1 failed to spawn");
    let mut p2 = Command::new(args[sep + 1].as_os_str())
        .args(args.iter().skip(sep + 2))
        .stdin(Stdio::piped())
        .spawn()
        .expect("process 2 failed to spawn");
    let (sender, receiver) = sync_channel(1024);
    let t1 = thread::spawn(move || {
        let p1_out = p1.stdout.as_mut().unwrap();
        loop {
            let mut buf = vec![MaybeUninit::<u8>::uninit(); 32768];
            let size = p1_out
                .read(unsafe { MaybeUninit::slice_assume_init_mut(&mut buf) })
                .unwrap();
            if size == 0 {
                break;
            }
            if sender
                .send(
                    buf.into_iter()
                        .take(size)
                        .map(|byte| unsafe { byte.assume_init() })
                        .collect::<Vec<u8>>(),
                )
                .is_err()
            {
                break;
            }
        }
        p1.wait().expect("process 1 failed");
    });
    let t2 = thread::spawn(move || {
        let p2_in = p2.stdin.as_mut().unwrap();
        loop {
            let pieces: Vec<_> = receiver.iter().take(1).chain(receiver.try_iter()).collect();
            if pieces.is_empty() {
                break;
            }
            let mut slices: Vec<_> = pieces
                .iter()
                .map(|piece| IoSlice::new(piece.as_slice()))
                .collect();
            if p2_in.write_all_vectored(slices.as_mut_slice()).is_err() {
                break;
            }
        }
        p2.wait().expect("process 2 failed");
    });
    t1.join().unwrap();
    t2.join().unwrap();
}
