// Copyright (C) 2024 Apple Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.

#if os(visionOS)

import AVFoundation
import Combine
import RealityKit
import UIKit
import os

#if canImport(AVKit, _version: 1270)
@_spi(LinearMediaKit) import AVKit
#else
import LinearMediaKit
#endif

private extension Logger {
    static let linearMediaPlayer = Logger(subsystem: "com.apple.WebKit", category: "Fullscreen")
}

private class SwiftOnlyData: NSObject {
    @Published var renderingConfiguration: RenderingConfiguration?
    @Published var thumbnailMaterial: VideoMaterial?
    @Published var videoMaterial: VideoMaterial?
    @Published var peculiarEntity: PeculiarEntity?

    // FIXME: It should be possible to store these directly on WKSLinearMediaPlayer since they are
    // bridged to NSDate, but a bug prevents that from compiling (rdar://121877511).
    @Published var startDate: Date?
    @Published var endDate: Date?

    @Published var presentationMode: PresentationMode = .inline
    @Published var presentationState: WKSLinearMediaPresentationState = .inline

    // FIXME: Publish fullscreenSceneBehaviors once rdar://122435030 is resolved
    var fullscreenBehaviorsSubject = CurrentValueSubject<[FullscreenBehaviors], Never>(FullscreenBehaviors.default)

    // Will be set to true if we entered via Docking Environment button (inline) or false otherwise.
    var enteredFromInline = false

    var spatialVideoMetadata: WKSLinearMediaSpatialVideoMetadata?
    var videoReceiverEndpointObserver: Cancellable?

    var isImmersiveVideo = false
    weak var viewController: WKSPlayableViewControllerHost?
    weak var defaultEntity: Entity?
}

enum LinearMediaPlayerErrors: Error {
    case invalidStateError
}

@objc @implementation extension WKSLinearMediaPlayer {
    weak var delegate: WKSLinearMediaPlayerDelegate?

    var selectedPlaybackRate = 1.0
    var error: Error?
    var canTogglePlayback = false
    var requiresLinearPlayback = false
    var interstitialRanges: [WKSLinearMediaTimeRange] = []
    var isInterstitialActive = false
    var duration: TimeInterval = .nan
    var currentTime: TimeInterval = .nan
    var remainingTime: TimeInterval = .nan
    var playbackRate = 0.0
    var playbackRates: [NSNumber] = [0.5, 1.0, 1.25, 1.5, 2.0]
    var isLoading = false
    var isTrimming = false
    var trimView: UIView?
    var thumbnailLayer: CALayer?
    var captionLayer: CALayer?
    var captionContentInsets: UIEdgeInsets = .zero
    var showsPlaybackControls = false
    var canSeek = false
    var seekableTimeRanges: [WKSLinearMediaTimeRange] = []
    var isSeeking = false
    var canScanBackward = false
    var canScanForward = false
    var contentInfoViewControllers: [UIViewController] = []
    var contextualActions: [UIAction] = []
    var contextualActionsInfoView: UIView?
    var contentDimensions = CGSize(width: 0, height: 0)
    var contentMode: WKSLinearMediaContentMode = .default
    var videoLayer: CALayer?
    var anticipatedViewingMode: WKSLinearMediaViewingMode = .none
    var contentOverlay: UIView?
    var contentOverlayViewController: UIViewController?
    var volume = 1.0
    var isMuted = false
    var sessionDisplayTitle: String?
    var sessionThumbnail: UIImage?
    var isSessionExtended = false
    var hasAudioContent = true
    var currentAudioTrack: WKSLinearMediaTrack?
    var audioTracks: [WKSLinearMediaTrack] = []
    var currentLegibleTrack: WKSLinearMediaTrack?
    var legibleTracks: [WKSLinearMediaTrack] = []
    var contentType: WKSLinearMediaContentType = .none
    var contentMetadata: WKSLinearMediaContentMetadata = .init(title: nil, subtitle: nil)
    var transportBarIncludesTitleView = true
    var artwork: Data?
    var isPlayableOffline = false
    var allowPip = true
    var allowFullScreenFromInline = true
    var isLiveStream = false
    var recommendedViewingRatio: NSNumber?
    var startTime: Double = .nan
    var endTime: Double = .nan
    var spatialImmersive = false
    var spatialVideoMetadata: WKSLinearMediaSpatialVideoMetadata? {
        get { swiftOnlyData.spatialVideoMetadata }
        set {
            swiftOnlyData.spatialVideoMetadata = newValue
            swiftOnlyData.peculiarEntity?.setVideoMetaData(to: swiftOnlyData.spatialVideoMetadata?.metadata)
        }
    }
    var isImmersiveVideo: Bool {
        get { swiftOnlyData.isImmersiveVideo }
        set {
            Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(newValue)")
            swiftOnlyData.isImmersiveVideo = newValue
            // FIXME: Should limit ContentTypePublisher to only publish changes to contentType if we have already created a default entity
            // rather than having to use a isImmersive attribute.
            if !swiftOnlyData.enteredFromInline && swiftOnlyData.defaultEntity != nil && swiftOnlyData.presentationState != .external  {
                contentType = newValue ? .immersive : .planar
            }
        }
    }
    var enteredFromInline: Bool {
        get { swiftOnlyData.enteredFromInline }
    }

    // FIXME: These should be stored properties on WKSLinearMediaPlayer, but a bug prevents that from compiling (rdar://121877511).
    var startDate: Date? {
        get { swiftOnlyData.startDate }
        set { swiftOnlyData.startDate = newValue }
    }
    var endDate: Date? {
        get { swiftOnlyData.endDate }
        set { swiftOnlyData.endDate = newValue }
    }

    var presentationState: WKSLinearMediaPresentationState {
        swiftOnlyData.presentationState
    }

    @nonobjc private var enterFullscreenCompletionHandler: ((Bool, (any Error)?) -> Void)?
    @nonobjc private var exitFullscreenCompletionHandler: ((Bool, (any Error)?) -> Void)?

    @nonobjc private var swiftOnlyData: SwiftOnlyData
    @nonobjc private var cancellables: [AnyCancellable] = []

    @nonobjc private final var logIdentifier: String {
        String(delegate?.linearMediaPlayerLogIdentifier?(self) ?? 0, radix: 16, uppercase: true)
    }

    private static let preferredTimescale: CMTimeScale = 600

    public override init() {
        swiftOnlyData = .init()
        super.init()
        swiftOnlyData.$presentationState
            .removeDuplicates()
            .sink { [unowned self] in presentationStateChanged($0) }
            .store(in: &cancellables)

        Logger.linearMediaPlayer.log("\(#function)")
    }

    // FIXME: Remove this override once rdar://108224957 is resolved.
    public override var description: String {
        "AVKit.AVPlayerPlayable"
    }

    func makeViewController() -> WKSPlayableViewControllerHost {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")

        if let viewController = swiftOnlyData.viewController {
            return viewController
        }
        let viewController = WKSPlayableViewControllerHost()
        viewController.playable = self
        viewController.prefersAutoDimming = true
        swiftOnlyData.viewController = viewController

        return viewController
    }

    func enterExternalPresentation(completionHandler: @MainActor @Sendable @escaping (Bool, (any Error)?) -> Void) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")

        switch presentationState {
        case .enteringFullscreen, .exitingFullscreen, .fullscreen, .external:
            completionHandler(false, LinearMediaPlayerErrors.invalidStateError)
        case .inline:
            contentType = .planar
            showsPlaybackControls = true
            swiftOnlyData.fullscreenBehaviorsSubject.send([ .hostContentInline ])
            swiftOnlyData.presentationState = .external
            contentOverlay = .init(frame: .zero)
            completionHandler(true, nil)
        @unknown default:
            fatalError()
        }
    }

    func exitExternalPresentation(completionHandler: @MainActor @Sendable @escaping (Bool, (any Error)?) -> Void) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")

        switch presentationState {
        case .enteringFullscreen, .exitingFullscreen, .fullscreen, .inline:
            completionHandler(false, LinearMediaPlayerErrors.invalidStateError)
        case .external:
            delegate?.linearMediaPlayerClearVideoReceiverEndpoint?(self)
            swiftOnlyData.presentationState = .inline
            swiftOnlyData.fullscreenBehaviorsSubject.send(FullscreenBehaviors.default)
            contentOverlay = nil
            showsPlaybackControls = false
            contentType = .none
            completionHandler(true, nil)
        @unknown default:
            fatalError()
        }
    }

    func enterFullscreen(completionHandler: @MainActor @Sendable @escaping (Bool, (any Error)?) -> Void) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")

        if let enterFullscreenCompletionHandler = enterFullscreenCompletionHandler {
            Logger.linearMediaPlayer.error("\(#function)(\(self.logIdentifier, privacy: .public)): invalidating existing enterFullscreenCompletionHandler")
            enterFullscreenCompletionHandler(false, LinearMediaPlayerErrors.invalidStateError)
            self.enterFullscreenCompletionHandler = nil
        }

        maybeCreateSpatialOrImmersiveEntity()

        switch presentationState {
        case .inline, .enteringFullscreen, .exitingFullscreen:
            enterFullscreenCompletionHandler = completionHandler
            swiftOnlyData.presentationState = .fullscreen
        case .fullscreen:
            completionHandler(true, nil)
        case .external:
            completionHandler(false, LinearMediaPlayerErrors.invalidStateError)
        @unknown default:
            fatalError()
        }
    }

    func exitFullscreen(completionHandler: @MainActor @Sendable @escaping (Bool, (any Error)?) -> Void) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")

        if let exitFullscreenCompletionHandler = exitFullscreenCompletionHandler {
            Logger.linearMediaPlayer.error("\(#function)(\(self.logIdentifier, privacy: .public)): invalidating existing exitFullscreenCompletionHandler")
            exitFullscreenCompletionHandler(false, LinearMediaPlayerErrors.invalidStateError)
            self.exitFullscreenCompletionHandler = nil
        }

        switch presentationState {
        case .exitingFullscreen, .fullscreen, .enteringFullscreen:
            exitFullscreenCompletionHandler = completionHandler
            swiftOnlyData.presentationState = .inline
        case .inline:
            completionHandler(true, nil)
        case .external:
            completionHandler(false, LinearMediaPlayerErrors.invalidStateError)
        @unknown default:
            fatalError()
        }
    }
}

extension WKSLinearMediaPlayer {
    private func presentationStateChanged(_ presentationState: WKSLinearMediaPresentationState) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)): \(presentationState, privacy: .public)")

        switch presentationState {
        case .inline:
            swiftOnlyData.presentationMode = .inline
        case .enteringFullscreen:
            delegate?.linearMediaPlayerEnterFullscreen?(self)
        case .fullscreen, .external:
            swiftOnlyData.presentationMode = .fullscreenFromInline
        case .exitingFullscreen:
            delegate?.linearMediaPlayerExitFullscreen?(self)
        @unknown default:
            fatalError()
        }
    }

    private func maybeCreateSpatialOrImmersiveEntity() {
        if swiftOnlyData.peculiarEntity != nil || contentType == .immersive { return }
        if swiftOnlyData.isImmersiveVideo {
            contentType = .immersive
            return
        }
        if swiftOnlyData.enteredFromInline || swiftOnlyData.spatialVideoMetadata == nil {
            contentType = .planar
            return
        }
        let metadata = swiftOnlyData.spatialVideoMetadata!
        swiftOnlyData.peculiarEntity = ContentType.makeSpatialEntity(videoMetadata: metadata.metadata, extruded: true)
        swiftOnlyData.peculiarEntity?.screenMode = spatialImmersive ? .immersive : .portal
// FIXME (147782145): Define a clang module for XPC to be used in Public SDK builds
#if canImport(XPC)
        swiftOnlyData.videoReceiverEndpointObserver = swiftOnlyData.peculiarEntity?.videoReceiverEndpointPublisher.sink {
            [weak self] in guard let endpoint = $0 else { return }
            self?.setVideoReceiverEndpoint(endpoint)
        }
#endif
        contentType = .spatial
    }

    private func maybeClearSpatialOrImmersiveEntity() {
        if swiftOnlyData.isImmersiveVideo && contentType == .immersive {
            contentType = .none
            return
        }
        if swiftOnlyData.peculiarEntity == nil { return }
        swiftOnlyData.videoReceiverEndpointObserver = nil
        swiftOnlyData.peculiarEntity = nil
        contentType = .none // this causes a call to makeDefaultEntity
    }
}

#endif // os(visionOS)

#if compiler(>=6.0)
#if os(visionOS)
@_spi(Internal) extension WKSLinearMediaPlayer: @preconcurrency Playable {
}
#endif
#else
#if os(visionOS)
@_spi(Internal) extension WKSLinearMediaPlayer: Playable {
}
#endif
#endif

#if os(visionOS)

@_spi(Internal) extension WKSLinearMediaPlayer {
    public var selectedPlaybackRatePublisher: AnyPublisher<Double, Never> {
        publisher(for: \.selectedPlaybackRate).eraseToAnyPublisher()
    }

    public var presentationModePublisher: AnyPublisher<PresentationMode, Never> {
        swiftOnlyData.$presentationMode.eraseToAnyPublisher()
    }

    public var errorPublisher: AnyPublisher<Error?, Never> {
        publisher(for: \.error).eraseToAnyPublisher()
    }

    public var canTogglePlaybackPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.canTogglePlayback).eraseToAnyPublisher()
    }

    public var requiresLinearPlaybackPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.requiresLinearPlayback).eraseToAnyPublisher()
    }

    public var interstitialRangesPublisher: AnyPublisher<[Range<TimeInterval>], Never> {
        publisher(for: \.interstitialRanges).map { $0.map { $0.range } }.eraseToAnyPublisher()
    }

    public var isInterstitialActivePublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isInterstitialActive).eraseToAnyPublisher()
    }

    public var durationPublisher: AnyPublisher<TimeInterval, Never> {
        publisher(for: \.duration).eraseToAnyPublisher()
    }

    public var currentTimePublisher: AnyPublisher<TimeInterval, Never> {
        publisher(for: \.currentTime).eraseToAnyPublisher()
    }

    public var remainingTimePublisher: AnyPublisher<TimeInterval, Never> {
        publisher(for: \.remainingTime).eraseToAnyPublisher()
    }

    public var playbackRatePublisher: AnyPublisher<Double, Never> {
        publisher(for: \.playbackRate).eraseToAnyPublisher()
    }

    public var playbackRatesPublisher: AnyPublisher<[Double], Never> {
        publisher(for: \.playbackRates).map { $0.map { $0.doubleValue } }.eraseToAnyPublisher()
    }

    public var isPlayingPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.playbackRate).map { $0 != 0.0 }.eraseToAnyPublisher()
    }

    public var isLoadingPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isLoading).eraseToAnyPublisher()
    }

    public var isTrimmingPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isTrimming).eraseToAnyPublisher()
    }

    public var forwardPlaybackEndTimePublisher: AnyPublisher<CMTime?, Never> {
        publisher(for: \.endTime)
            .dropFirst()
            .map { $0.isNaN ? .invalid : CMTime(seconds: $0, preferredTimescale: Self.preferredTimescale) }
            .eraseToAnyPublisher()
    }

    public var reversePlaybackEndTimePublisher: AnyPublisher<CMTime?, Never> {
        publisher(for: \.startTime)
            .dropFirst()
            .map { $0.isNaN ? .invalid : CMTime(seconds: $0, preferredTimescale: Self.preferredTimescale) }
            .eraseToAnyPublisher()
    }

    public var trimViewPublisher: AnyPublisher<UIView?, Never> {
        publisher(for: \.trimView).eraseToAnyPublisher()
    }

    public var thumbnailLayerPublisher: AnyPublisher<CALayer?, Never> {
        publisher(for: \.thumbnailLayer).eraseToAnyPublisher()
    }

    public var thumbnailMaterialPublisher: AnyPublisher<VideoMaterial?, Never> {
        swiftOnlyData.$thumbnailMaterial.eraseToAnyPublisher()
    }

    public var captionLayerPublisher: AnyPublisher<CALayer?, Never> {
        Just(nil).eraseToAnyPublisher()
    }

    public var captionContentInsetsPublisher: AnyPublisher<UIEdgeInsets, Never> {
        publisher(for: \.captionContentInsets).eraseToAnyPublisher()
    }

    public var showsPlaybackControlsPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.showsPlaybackControls).eraseToAnyPublisher()
    }

    public var canSeekPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.canSeek).eraseToAnyPublisher()
    }

    public var seekableTimeRangesPublisher: AnyPublisher<[ClosedRange<TimeInterval>], Never> {
        publisher(for: \.seekableTimeRanges).map { $0.map { $0.closedRange } }.eraseToAnyPublisher()
    }

    public var isSeekingPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isSeeking).eraseToAnyPublisher()
    }

    public var canScanBackwardPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.canScanBackward).eraseToAnyPublisher()
    }

    public var canScanForwardPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.canScanForward).eraseToAnyPublisher()
    }

    public var contentInfoViewControllersPublisher: AnyPublisher<[UIViewController], Never> {
        publisher(for: \.contentInfoViewControllers).eraseToAnyPublisher()
    }

    public var contextualActionsPublisher: AnyPublisher<[UIAction], Never> {
        publisher(for: \.contextualActions).eraseToAnyPublisher()
    }

    public var contextualActionsInfoViewPublisher: AnyPublisher<UIView?, Never> {
        publisher(for: \.contextualActionsInfoView).eraseToAnyPublisher()
    }

    public var contentDimensionsPublisher: AnyPublisher<CGSize, Never> {
        publisher(for: \.contentDimensions).eraseToAnyPublisher()
    }

    public var contentModePublisher: AnyPublisher<ContentMode, Never> {
        publisher(for: \.contentMode).compactMap { $0.contentMode }.eraseToAnyPublisher()
    }

    public var videoLayerPublisher: AnyPublisher<CALayer?, Never> {
        publisher(for: \.videoLayer).eraseToAnyPublisher()
    }

    public var videoMaterialPublisher: AnyPublisher<VideoMaterial?, Never> {
        swiftOnlyData.$videoMaterial.eraseToAnyPublisher()
    }

    public var peculiarEntityPublisher: AnyPublisher<PeculiarEntity?, Never> {
        swiftOnlyData.$peculiarEntity.eraseToAnyPublisher()
    }

    public var anticipatedViewingModePublisher: AnyPublisher<ViewingMode?, Never> {
        publisher(for: \.anticipatedViewingMode).compactMap { $0.viewingMode }.eraseToAnyPublisher()
    }

    public var contentOverlayPublisher: AnyPublisher<UIView?, Never> {
        publisher(for: \.contentOverlay).eraseToAnyPublisher()
    }

    public var contentOverlayViewControllerPublisher: AnyPublisher<UIViewController?, Never> {
        publisher(for: \.contentOverlayViewController).eraseToAnyPublisher()
    }

    public var volumePublisher: AnyPublisher<Double, Never> {
        publisher(for: \.volume).eraseToAnyPublisher()
    }

    public var isMutedPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isMuted).eraseToAnyPublisher()
    }
#if !canImport(AVKit, _version: 1270)
    public var sessionDisplayTitlePublisher: AnyPublisher<String?, Never> {
        publisher(for: \.sessionDisplayTitle).eraseToAnyPublisher()
    }
#endif
    public var sessionThumbnailPublisher: AnyPublisher<UIImage?, Never> {
        publisher(for: \.sessionThumbnail).eraseToAnyPublisher()
    }

    public var isSessionExtendedPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isSessionExtended).eraseToAnyPublisher()
    }

    public var hasAudioContentPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.hasAudioContent).eraseToAnyPublisher()
    }

    public var currentAudioTrackPublisher: AnyPublisher<Track?, Never> {
        publisher(for: \.currentAudioTrack).map { $0 }.eraseToAnyPublisher()
    }

    public var audioTracksPublisher: AnyPublisher<[Track]?, Never> {
        publisher(for: \.audioTracks).map { $0 }.eraseToAnyPublisher()
    }

    public var currentLegibleTrackPublisher: AnyPublisher<Track?, Never> {
        publisher(for: \.currentLegibleTrack).map { $0 }.eraseToAnyPublisher()
    }

    public var legibleTracksPublisher: AnyPublisher<[Track]?, Never> {
        publisher(for: \.legibleTracks).map { $0 }.eraseToAnyPublisher()
    }

    public var contentTypePublisher: AnyPublisher<ContentType?, Never> {
        publisher(for: \.contentType).map { $0.contentType }.eraseToAnyPublisher()
    }

    public var contentMetadataPublisher: AnyPublisher<ContentMetadataContainer, Never> {
        publisher(for: \.contentMetadata).map { $0.contentMetadata }.eraseToAnyPublisher()
    }

    public var transportBarIncludesTitleViewPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.transportBarIncludesTitleView).eraseToAnyPublisher()
    }

    public var artworkPublisher: AnyPublisher<Data?, Never> {
        publisher(for: \.artwork).eraseToAnyPublisher()
    }

    public var isPlayableOfflinePublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isPlayableOffline).eraseToAnyPublisher()
    }

    public var allowPipPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.allowPip).eraseToAnyPublisher()
    }

    public var allowFullScreenFromInlinePublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.allowFullScreenFromInline).eraseToAnyPublisher()
    }

    public var isLiveStreamPublisher: AnyPublisher<Bool, Never> {
        publisher(for: \.isLiveStream).eraseToAnyPublisher()
    }

    public var startDatePublisher: AnyPublisher<Date, Never> {
        swiftOnlyData.$startDate.compactMap { $0 }.eraseToAnyPublisher()
    }

    public var endDatePublisher: AnyPublisher<Date, Never> {
        swiftOnlyData.$endDate.compactMap { $0 }.eraseToAnyPublisher()
    }

    public var recommendedViewingRatioPublisher: AnyPublisher<Double?, Never> {
        publisher(for: \.recommendedViewingRatio).compactMap { $0?.doubleValue }.eraseToAnyPublisher()
    }

    public var fullscreenSceneBehaviorsPublisher: AnyPublisher<[FullscreenBehaviors], Never> {
        // FIXME: Publish fullscreenSceneBehaviors once rdar://122435030 is resolved
        swiftOnlyData.fullscreenBehaviorsSubject.eraseToAnyPublisher()
    }

    public func updateRenderingConfiguration(_ config: RenderingConfiguration) {
        swiftOnlyData.renderingConfiguration = config
    }

    public func play() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerPlay?(self)
    }

    public func pause() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerPause?(self)
    }

    public func togglePlayback() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerTogglePlayback?(self)
    }

    public func setPlaybackRate(_ rate: Double) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(rate)")
        delegate?.linearMediaPlayer?(self, setPlaybackRate: rate)
    }

    public func seek(to time: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(time)")
        delegate?.linearMediaPlayer?(self, seekToTime: time)
    }

    public func seek(delta: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(delta)")
        delegate?.linearMediaPlayer?(self, seekByDelta: delta)
    }

    public func seek(to destination: TimeInterval, from source: TimeInterval, metadata: SeekMetadata) -> TimeInterval {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) destination=\(destination) source=\(source)")
        return delegate?.linearMediaPlayer?(self, seekToDestination: destination, fromSource: source) ?? TimeInterval.zero
    }

    public func completeTrimming(commitChanges: Bool) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(commitChanges)")
        delegate?.linearMediaPlayer?(self, completeTrimming: commitChanges)
    }

    public func updateStartTime(_ time: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(time)")
        delegate?.linearMediaPlayer?(self, updateStartTime: time)
    }

    public func updateEndTime(_ time: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(time)")
        delegate?.linearMediaPlayer?(self, updateEndTime: time)
    }

    public func beginEditingVolume() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerBeginEditingVolume?(self)
    }

    public func endEditingVolume() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerEndEditingVolume?(self)
    }

    public func setAudioTrack(_ newTrack: Track?) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(newTrack?.localizedDisplayName ?? "nil")")
        delegate?.linearMediaPlayer?(self, setAudioTrack: newTrack as? WKSLinearMediaTrack)
    }

    public func setLegibleTrack(_ newTrack: Track?) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(newTrack?.localizedDisplayName ?? "nil")")
        delegate?.linearMediaPlayer?(self, setLegibleTrack: newTrack as? WKSLinearMediaTrack)
    }

    public func skipActiveInterstitial() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerSkipActiveInterstitial?(self)
    }

    public func setCaptionContentInsets(_ insets: UIEdgeInsets) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(NSCoder.string(for: insets), privacy: .public)")
        delegate?.linearMediaPlayer?(self, setCaptionContentInsets: insets)
    }

    public func updateVideoBounds(_ bounds: CGRect) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(NSCoder.string(for: bounds), privacy: .public)")
        delegate?.linearMediaPlayer?(self, updateVideoBounds: bounds)
    }

    public func updateViewingMode(_ mode: ViewingMode?) {
        let viewingMode = WKSLinearMediaViewingMode(mode)
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(viewingMode)")
        delegate?.linearMediaPlayer?(self, update: viewingMode)
    }

    public func togglePip() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerTogglePip?(self)
    }

    public func toggleInlineMode() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)): presentationState=\(self.presentationState, privacy: .public)")

        switch presentationState {
        case .inline:
            swiftOnlyData.presentationState = .enteringFullscreen
        case .fullscreen:
            swiftOnlyData.presentationState = .exitingFullscreen
        case .enteringFullscreen, .exitingFullscreen, .external:
            break
        @unknown default:
            fatalError()
        }
    }

    public func willEnterFullscreen() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)): presentationState=\(self.presentationState, privacy: .public)")

        switch presentationState {
        case .inline:
            swiftOnlyData.enteredFromInline = true
            swiftOnlyData.presentationState = .enteringFullscreen
        case .enteringFullscreen, .exitingFullscreen, .fullscreen, .external:
            break
        @unknown default:
            fatalError()
        }
    }

    public func didCompleteEnterFullscreen(result: Result<Void, any Error>) {
        let completionHandler = enterFullscreenCompletionHandler
        enterFullscreenCompletionHandler = nil

        switch result {
        case .success():
            Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)): success")
            completionHandler?(true, nil)
        case .failure(let error):
            Logger.linearMediaPlayer.error("\(#function)(\(self.logIdentifier, privacy: .public)): \(error)")
            completionHandler?(false, error)
        }
    }

    public func willExitFullscreen() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)): presentationState=\(self.presentationState, privacy: .public)")

        switch presentationState {
        case .fullscreen:
            swiftOnlyData.presentationState = .exitingFullscreen
        case .inline, .enteringFullscreen, .exitingFullscreen, .external:
            break
        @unknown default:
            fatalError()
        }
    }

    public func didCompleteExitFullscreen(result: Result<Void, any Error>) {
        let completionHandler = exitFullscreenCompletionHandler
        exitFullscreenCompletionHandler = nil
        maybeClearSpatialOrImmersiveEntity()
        swiftOnlyData.enteredFromInline = false

        switch result {
        case .success():
            Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)): success")
            completionHandler?(true, nil)
        case .failure(let error):
            Logger.linearMediaPlayer.error("\(#function)(\(self.logIdentifier, privacy: .public)): \(error)")
            completionHandler?(false, error)
        }
    }

    public func makeDefaultEntity() -> Entity? {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")

        // This gets called from maybeCreateSpatialOrImmersiveEntity through the KVO when setting
        // peculiarEntity. As such, we can't check if the peculiarEntity is set or not.
        // We will return nil here on the first call and will get call back again once
        // peculiarEntity is set.
        if swiftOnlyData.spatialVideoMetadata != nil && !swiftOnlyData.enteredFromInline && swiftOnlyData.presentationState != .external {
            return swiftOnlyData.peculiarEntity
        }
        if let captionLayer {
            let entity = ContentType.makeEntity(captionLayer: captionLayer)
            swiftOnlyData.defaultEntity = entity
            return entity
        }

        Logger.linearMediaPlayer.error("\(#function)(\(self.logIdentifier, privacy: .public)): failed to find spatialVideoMetadata and captionLayer")
        swiftOnlyData.defaultEntity = nil
        return nil
    }

    public func setTimeResolverInterval(_ interval: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(interval)")
        delegate?.linearMediaPlayer?(self, setTimeResolverInterval: interval)
    }

    public func setTimeResolverResolution(_ resolution: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(resolution)")
        delegate?.linearMediaPlayer?(self, setTimeResolverResolution: resolution)
    }

    public func setThumbnailSize(_ size: CGSize) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(NSCoder.string(for: size), privacy: .public)")
        delegate?.linearMediaPlayer?(self, setThumbnailSize: size)
    }

    public func seekThumbnail(to time: TimeInterval) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(time)")
        delegate?.linearMediaPlayer?(self, seekThumbnailToTime: time)
    }

    public func beginScrubbing() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerBeginScrubbing?(self)
    }

    public func endScrubbing() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerEndScrubbing?(self)
    }

    public func beginScanningForward() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerBeginScanningForward?(self)
    }

    public func endScanningForward() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerEndScanningForward?(self)
    }

    public func beginScanningBackward() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerBeginScanningBackward?(self)
    }

    public func endScanningBackward() {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayerEndScanningBackward?(self)
    }

    public func setVolume(_ volume: Double) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(volume)")
        delegate?.linearMediaPlayer?(self, setVolume: volume)
    }

    public func setIsMuted(_ value: Bool) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public)) \(value)")
        delegate?.linearMediaPlayer?(self, setMuted: value)
    }

// FIXME (147782145): Define a clang module for XPC to be used in Public SDK builds
#if canImport(XPC)
    public func setVideoReceiverEndpoint(_ endpoint: xpc_object_t) {
        Logger.linearMediaPlayer.log("\(#function)(\(self.logIdentifier, privacy: .public))")
        delegate?.linearMediaPlayer?(self, setVideoReceiverEndpoint: endpoint)
    }
#endif
}

#endif // os(visionOS)
