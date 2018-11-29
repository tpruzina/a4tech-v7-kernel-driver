a4tech-bloody-driver
====================

A4tech Bloody mice driver for linux kernel

STATUS: DEAD (I don't own any A4Tech mices anymore)

Simple PoC driver based on reversing windows driver with wireshark+usbmon. 

(ignore) HELP WANTED
=====================

If you have some A4Tech bloody mouse variant, please create ticket on bugzilla and post following:

Product ID (listable by lsusb, appears in form of vendorid:productid in hex)
HID descriptors (listable by lsusb -v -d vendorid:productid)

I have access to 2 bloody V7 mices atm and as far I can tell they behave identically
(albeit they differ in product IDs and revisions).
