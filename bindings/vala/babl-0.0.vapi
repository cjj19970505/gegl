/* babl.vapi generated by vapigen, do not modify. */

[CCode (cprefix = "Babl", lower_case_cprefix = "babl_")]
namespace Babl {
	[CCode (cname = "Babl", cheader_filename = "babl/babl.h")]
	public class Format {
		public Format ();
	}
	[CCode (cname = "Babl", cheader_filename = "babl/babl.h")]
	public class Component {
		public Component ();
	}
	[CCode (cheader_filename = "babl/babl.h")]
	public static void init ();
	[CCode (cheader_filename = "babl/babl.h")]
	public static void destroy ();
	[CCode (cheader_filename = "babl/babl.h")]
	public static unowned Babl.Format format (string name);
}