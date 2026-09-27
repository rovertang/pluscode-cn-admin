declare module 'open-location-code' {
    const library: {
        OpenLocationCode: new () => {
            isFull(code: string): boolean;
            encode(latitude: number, longitude: number, codeLength?: number): string;
        };
    };
    export default library;
}
