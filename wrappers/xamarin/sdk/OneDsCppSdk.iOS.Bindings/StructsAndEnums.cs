using System;
using ObjCRuntime;

namespace Microsoft.Applications.Events
{
	[Native]
	public enum EventPriority : long
	{
		Unspecified = -1,
		Off = 0,
		Low = 1,
		Normal = 2,
		High = 3,
		Immediate = 4
	}

	[Native]
	public enum PiiKind : long
	{
		None = 0,
		DistinguishedName = 1,
		GenericData = 2,
		IPv4Address = 3,
		IPv6Address = 4,
		MailSubject = 5,
		PhoneNumber = 6,
		QueryString = 7,
		SipAddress = 8,
		SmtpAddress = 9,
		Identity = 10,
		Uri = 11,
		Fqdn = 12,
		IPV4AddressLegacy = 13
	}

	[Flags]
	public enum PrivacyDataType : ulong
	{
		BrowsingHistory = 0x2L,
		DeviceConnectivityAndConfiguration = 0x800L,
		InkingTypingAndSpeechUtterance = 0x20000L,
		ProductAndServicePerformance = 0x1000000L,
		ProductAndServiceUsage = 0x2000000L,
		SoftwareSetupAndInventory = 0x80000000L
	}

	public enum DiagLevel
	{
		RequiredDiagnosticData = 1,
		OptionalDiagnosticData = 2,
		RequiredServiceData = 110,
		RequiredServiceDataForEssentialServices = 120
	}

	[Native]
	public enum TraceLevel : long
	{
		None = 0,
		Error = 1,
		Warning = 2,
		Information = 3,
		Verbose = 4
	}

	[Native]
	public enum SessionState : long
	{
		Started = 0,
		Ended = 1
	}

	[Native]
	public enum TransmissionProfile : long
	{
		RealTime = 0,
		NearRealTime = 1,
		BestEffort = 2
	}
}
